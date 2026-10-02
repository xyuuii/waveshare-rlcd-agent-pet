import { decodeFrames, parseRla } from "./rla-codec.js";
import {
  FrameProcessor,
  PanelPainter,
  SIZE_PRESETS,
  convertVideo,
  detectBorders,
  loadVideo,
  sanitizeAnimationId,
  seekTo,
  suggestAnimationId,
  supportsFrameCallbacks
} from "./converter.js";
import { describeTrim, frameLayout } from "./framing.js";
import { ServerClock, SoundSync } from "./sound-sync.js";

// ---------------------------------------------------------------- constants

const FACES = [
  { id: "sans", name: "大字", tag: "SANS" },
  { id: "segment", name: "七段数码", tag: "LCD" },
  { id: "dots", name: "点阵", tag: "DOTS" },
  { id: "analog", name: "指针表盘", tag: "DIAL" },
  { id: "words", name: "英文字钟", tag: "WORDS" },
  { id: "terminal", name: "终端", tag: "TERM" },
  { id: "pet", name: "宠物", tag: "PET" }
];

const STATUS = {
  running: ["工作中", "working"],
  working: ["工作中", "working"],
  started: ["开始了", "working"],
  progress: ["进行中", "working"],
  "near-complete": ["快完成了", "working"],
  thinking: ["思考中", "working"],
  searching: ["搜索中", "working"],
  "tool-use": ["调用工具", "working"],
  "needs-attention": ["等你处理", "attn"],
  completed: ["完成", "done"],
  error: ["出错", "error"],
  failed: ["出错", "error"],
  idle: ["空闲", "idle"]
};

const FAMILY = {
  codex: ["Codex", "CX"],
  claude: ["Claude Code", "CC"],
  hermes: ["Hermes", "HM"],
  openclaw: ["OpenClaw", "OC"]
};

const PAGE_NAMES = { overview: "概览", usage: "用量", clock: "时钟" };
const STORAGE_TOKEN = "rlcdpet.token";
const STORAGE_CLIPS = "rlcdpet.clips";
const STORAGE_OFFSET = "rlcdpet.audioOffset";
const STORAGE_LATENCY = "rlcdpet.soundStartLatency";
const STORAGE_FRAMING = "rlcdpet.framing";

// ---------------------------------------------------------------- helpers

const $ = (selector) => document.querySelector(selector);

function store(key, value) {
  try {
    if (value === null || value === undefined || value === "") localStorage.removeItem(key);
    else localStorage.setItem(key, typeof value === "string" ? value : JSON.stringify(value));
  } catch {
    // private window or blocked storage: the page still works without it
  }
}

function recall(key, parse = false) {
  try {
    const value = localStorage.getItem(key);
    if (value === null) return null;
    return parse ? JSON.parse(value) : value;
  } catch {
    return null;
  }
}

function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (value === undefined || value === null || value === false) continue;
    if (key === "class") node.className = value;
    else if (key === "text") node.textContent = value;
    else if (key.startsWith("on")) node.addEventListener(key.slice(2), value);
    else node.setAttribute(key, value === true ? "" : String(value));
  }
  for (const child of children) {
    if (child === null || child === undefined || child === false) continue;
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
}

function familyOf(source) {
  const text = String(source || "").toLowerCase();
  if (text.includes("claude")) return "claude";
  if (text.includes("codex")) return "codex";
  if (text.includes("hermes")) return "hermes";
  if (text.includes("openclaw")) return "openclaw";
  return "";
}

function statusInfo(status) {
  return STATUS[status] || [status || "空闲", "idle"];
}

function prettyTask(task) {
  const text = String(task || "");
  if (!text || /(^|-)(codex-runtime|claude-session|agent-task)$/.test(text)) return "";
  return text;
}

const serverClock = new ServerClock();

function serverNow() {
  return Date.now() + serverClock.offsetMs;
}

function timeAgo(value) {
  const time = typeof value === "number" ? value : Date.parse(value || "");
  if (!Number.isFinite(time)) return "";
  const seconds = Math.max(0, Math.round((serverNow() - time) / 1000));
  if (seconds < 5) return "刚刚";
  if (seconds < 60) return `${seconds} 秒前`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)} 分钟前`;
  if (seconds < 86400) return `${Math.floor(seconds / 3600)} 小时前`;
  return `${Math.floor(seconds / 86400)} 天前`;
}

function formatDuration(totalSeconds) {
  if (!Number.isFinite(totalSeconds)) return "—";
  const seconds = Math.max(0, Math.round(totalSeconds));
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  if (h) return `${h}:${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}`;
  return `${m}:${String(s).padStart(2, "0")}`;
}

function formatUptime(totalSeconds) {
  if (!Number.isFinite(totalSeconds)) return "—";
  const days = Math.floor(totalSeconds / 86400);
  const hours = Math.floor((totalSeconds % 86400) / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  if (days) return `${days} 天 ${hours} 小时`;
  if (hours) return `${hours} 小时 ${minutes} 分`;
  return `${minutes} 分钟`;
}

function formatBytes(bytes) {
  if (!Number.isFinite(bytes)) return "—";
  if (bytes >= 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  if (bytes >= 1024) return `${(bytes / 1024).toFixed(0)} KB`;
  return `${bytes} B`;
}

function abbrev(value) {
  if (!Number.isFinite(value)) return "—";
  if (value >= 1_000_000) return `${(value / 1_000_000).toFixed(1)}M`;
  if (value >= 1000) return `${Math.round(value / 1000)}k`;
  return String(value);
}

let toastTimer = 0;
function toast(message, kind = "info") {
  const node = $("#toast");
  node.textContent = message;
  node.dataset.kind = kind;
  node.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => node.classList.remove("show"), kind === "error" ? 5200 : 2600);
}

// ---------------------------------------------------------------- state & API

const state = {
  token: "",
  status: null,
  failures: 0,
  // Bumped whenever this page starts or stops an egg, so a /status reply that
  // was already under way does not undo it.
  eggEpoch: 0,
  pendingSettings: null,
  file: null,
  fileUrl: "",
  videoMeta: null,
  // Black borders baked into the loaded video: { url, busy, done, box }
  borders: null,
  mode: "threshold",
  converting: null,
  lastConverted: null,
  deleteArmed: "",
  eggRev: 0
};

class ApiError extends Error {
  constructor(message, status) {
    super(message);
    this.status = status;
  }
}

function initToken() {
  const hash = new URLSearchParams(location.hash.slice(1));
  const fromHash = hash.get("token");
  if (fromHash) {
    store(STORAGE_TOKEN, fromHash);
    history.replaceState(null, "", location.pathname + location.search);
  }
  state.token = fromHash || recall(STORAGE_TOKEN) || "";
}

async function api(path, { method = "GET", json, body, raw = false } = {}) {
  const headers = {};
  if (state.token) headers.authorization = `Bearer ${state.token}`;
  let payload = body;
  if (json !== undefined) {
    headers["content-type"] = "application/json";
    payload = JSON.stringify(json);
  } else if (body instanceof Uint8Array) {
    headers["content-type"] = "application/octet-stream";
  }
  const sent = Date.now();
  const response = await fetch(path, { method, headers, body: payload, cache: "no-store" });
  if (response.status === 401) {
    askForToken();
    throw new ApiError("需要访问令牌", 401);
  }
  if (raw) {
    if (!response.ok) throw new ApiError(`${response.status} ${response.statusText}`, response.status);
    return response;
  }
  const data = await response.json().catch(() => ({}));
  if (!response.ok || data.ok === false) {
    const message = data.error || (data.errors && data.errors.join("；")) || `${response.status} ${response.statusText}`;
    throw new ApiError(message, response.status);
  }
  if (Number.isFinite(data.serverTimeMs)) serverClock.note(data.serverTimeMs, sent, Date.now());
  return data;
}

function askForToken() {
  const dialog = $("#tokenDialog");
  if (!dialog.open) {
    $("#tokenInput").value = "";
    dialog.showModal();
  }
}

// ---------------------------------------------------------------- polling

let pollTimer = 0;
async function refresh() {
  const epoch = state.eggEpoch;
  try {
    const status = await api("/status");
    state.status = status;
    state.failures = 0;
    render(status);
    if (epoch === state.eggEpoch) syncMirrorWithEgg(status.egg);
  } catch (error) {
    state.failures += 1;
    if (error.status !== 401) renderBridgeDown(error);
  }
}

function schedule(delay) {
  clearTimeout(pollTimer);
  pollTimer = setTimeout(async () => {
    await refresh();
    schedule();
  }, delay ?? (document.hidden ? 10_000 : 2_000));
}

document.addEventListener("visibilitychange", () => {
  if (!document.hidden) schedule(0);
});

// ---------------------------------------------------------------- rendering

function setPill(node, stateName, text) {
  node.dataset.state = stateName;
  node.querySelector(".label").textContent = text;
}

function renderBridgeDown(error) {
  setPill($("#bridgePill"), "bad", state.failures > 1 ? "Bridge 连不上" : "Bridge 重试中…");
  $("#footerBridge").textContent = error?.message ? `最近一次错误：${error.message}` : "";
}

function render(status) {
  renderPills(status);
  renderNow(status);
  renderBoard(status);
  renderFaces(status);
  renderTimezone(status);
  renderLibrary(status);
}

function renderPills(status) {
  const bridge = status.bridge || {};
  setPill($("#bridgePill"), "ok", `Bridge ${bridge.version || ""} · ${bridge.tokenRequired ? "令牌保护" : "仅本机"}`);
  const device = status.device || {};
  if (!device.seen) setPill($("#boardPill"), "idle", "板子 未连接");
  else if (device.online) {
    const battery = Number.isFinite(device.battery) ? ` · ${device.battery}%` : "";
    setPill($("#boardPill"), "ok", `板子 在线${battery}`);
  } else setPill($("#boardPill"), "warn", `板子 离线 · ${timeAgo(device.lastSeenAt)}`);
  $("#footerBridge").textContent = `Bridge ${bridge.version || ""} · 已运行 ${formatUptime(bridge.uptimeS)}`;
}

function renderNow(status) {
  const current = status.current;
  const hero = $("#nowHero");
  const [label, tone] = statusInfo(current?.status || "idle");
  hero.dataset.status = tone;
  $("#nowStatus").textContent = label;
  const family = familyOf(current?.source);
  $("#nowSource").textContent = current ? FAMILY[family]?.[0] || current.source || "智能体" : "还没有智能体活动";
  $("#nowTask").textContent = current ? prettyTask(current.task) : "";
  $("#nowUpdated").textContent = current ? `${current.source} · ${timeAgo(current.time)}` : "";
  document.title = current ? `${label} · RLCD Pet` : "RLCD Pet 控制台";

  const detail = $("#nowDetail");
  detail.replaceChildren();
  const claude = status.realtime?.claude;
  if (family === "claude" && claude) {
    if (claude.tool) detail.append(el("span", { text: `工具 ${claude.tool}` }));
    if (claude.model) detail.append(el("span", { text: `模型 ${claude.model}` }));
    if (claude.context?.window) {
      const ratio = Math.min(1, claude.context.used / claude.context.window);
      detail.append(
        el(
          "span",
          { class: "meter", title: "上下文占用" },
          "上下文",
          el("span", { class: "track" }, el("span", { class: "fill", style: `width:${(ratio * 100).toFixed(1)}%` })),
          `${abbrev(claude.context.used)} / ${abbrev(claude.context.window)}`
        )
      );
    }
    if (claude.project) detail.append(el("span", { text: `项目 ${claude.project}` }));
  }

  const usage = status.usage || {};
  $("#usageTodayLabel").textContent = usage.today_label || "TODAY";
  $("#usageToday").textContent = usage.today || "--";
  $("#usageToday").title = usage.today_hint || "";
  $("#usageContextLabel").textContent = usage.context_label || "CONTEXT";
  $("#usageContext").textContent = usage.context || "--";
  $("#usageContext").title = usage.context_hint || "";
  $("#usageQuotaLabel").textContent = usage.quota_label || "QUOTA";
  $("#usageQuota").textContent = usage.quota || "--";
  $("#usageQuota").title = usage.quota_hint || "";

  const agents = status.agents || [];
  $("#agentCount").textContent = agents.length ? `${agents.length}` : "";
  const list = $("#agentList");
  list.replaceChildren(
    ...agents.map((agent) => {
      const agentFamily = agent.family || familyOf(agent.source);
      const [chipLabel, chipTone] = statusInfo(agent.status);
      const title = agent.project || prettyTask(agent.task) || FAMILY[agentFamily]?.[0] || agent.source;
      return el(
        "li",
        { class: "agent" },
        el("span", { class: "family", title: FAMILY[agentFamily]?.[0] || agent.source, text: FAMILY[agentFamily]?.[1] || "AI" }),
        el(
          "div",
          { class: "agent-main" },
          el("div", { class: "agent-title", text: title }),
          el("div", { class: "agent-sub", text: `${agent.source} · ${timeAgo(agent.updated_at)}` })
        ),
        el("span", { class: "chip", "data-status": chipTone, text: chipLabel })
      );
    })
  );
  if (!agents.length) list.append(el("li", { class: "empty", text: "还没有收到任何智能体的状态。" }));
}

function renderBoard(status) {
  const device = status.device || {};
  $("#boardEmpty").hidden = Boolean(device.seen);
  $("#boardKv").hidden = !device.seen;
  $("#boardSeen").textContent = device.seen ? `最后一次 ${timeAgo(device.lastSeenAt)}` : "";
  for (const button of document.querySelectorAll("#pageButtons button")) {
    button.setAttribute("aria-pressed", String(device.page === button.dataset.page));
  }
  if (!device.seen) return;

  const rows = [];
  rows.push(["固件", device.firmware || "旧版（无遥测）"]);
  if (Number.isFinite(device.battery)) {
    const battery = el(
      "span",
      { class: "battery" },
      el("span", { class: "cell" }, el("span", { style: `width:${Math.max(4, Math.min(100, device.battery))}%` })),
      `${device.battery}%${device.charging ? " 充电中" : ""}${Number.isFinite(device.batteryMv) ? ` · ${(device.batteryMv / 1000).toFixed(2)} V` : ""}`
    );
    rows.push(["电量", battery]);
  }
  if (Number.isFinite(device.temperatureC)) {
    const humidity = Number.isFinite(device.humidity) ? ` · ${Math.round(device.humidity)}%` : "";
    rows.push(["温度 · 湿度", `${device.temperatureC.toFixed(1)} °C${humidity}`]);
  }
  if (Number.isFinite(device.rssi)) {
    const quality = device.rssi >= -60 ? "强" : device.rssi >= -72 ? "中" : "弱";
    rows.push(["Wi-Fi", `${device.rssi} dBm · ${quality}`]);
  }
  if (device.address) rows.push(["地址", device.address]);
  if (device.page) {
    const face = FACES.find((item) => item.id === device.clockStyle);
    rows.push(["页面", `${PAGE_NAMES[device.page] || device.page}${device.page === "clock" && face ? ` · ${face.name}` : ""}`]);
  }
  if (device.firmware) rows.push(["时间", device.timeValid ? "已校准" : "未校准"]);
  if (Number.isFinite(device.uptimeS)) rows.push(["运行", formatUptime(device.uptimeS)]);
  if (Number.isFinite(device.freeHeap)) rows.push(["空闲内存", formatBytes(device.freeHeap)]);
  $("#boardKv").replaceChildren(...rows.flatMap(([key, value]) => [el("dt", { text: key }), el("dd", {}, value)]));
}

function buildFaces() {
  const container = $("#faces");
  container.replaceChildren(
    ...FACES.map((face) =>
      el(
        "button",
        { type: "button", class: "face", "data-style": face.id, "aria-pressed": "false", onclick: () => chooseFace(face.id) },
        el(
          "div",
          { class: "screen" },
          el("img", { src: `previews/clock-${face.id}.png`, alt: `${face.name}表盘预览`, width: 400, height: 300, loading: "lazy" })
        ),
        el(
          "div",
          { class: "face-meta" },
          el("span", { class: "face-name", text: face.name }),
          el("span", { class: "on-board", text: "板子上" }),
          el("span", { class: "face-tag", text: face.tag })
        )
      )
    )
  );
}

function renderFaces(status) {
  const settings = state.pendingSettings || status.settings || {};
  const device = status.device || {};
  for (const button of document.querySelectorAll(".face")) {
    button.setAttribute("aria-pressed", String(button.dataset.style === settings.clockStyle));
    button.dataset.onBoard = String(device.online && device.clockStyle === button.dataset.style && device.page === "clock");
  }
  $("#hour12Toggle").checked = Boolean(settings.hour12);
  $("#secondsToggle").checked = settings.showSeconds !== false;
}

function renderTimezone(status) {
  const tz = status.timezone || {};
  $("#tzSummary").textContent = `${tz.zone || "?"} · ${tz.effective || "?"}${tz.override ? "（覆盖）" : ""}`;
  const input = $("#tzInput");
  if (document.activeElement !== input) input.value = tz.override || "";
}

// ---------------------------------------------------------------- settings

async function updateSettings(patch, successMessage) {
  const base = state.status?.settings || {};
  state.pendingSettings = { ...base, ...patch };
  if (state.status) renderFaces(state.status);
  try {
    const result = await api("/settings", { method: "POST", json: patch });
    if (state.status) state.status.settings = result.settings;
    if (successMessage) toast(successMessage);
  } catch (error) {
    toast(`没保存：${error.message}`, "error");
  } finally {
    state.pendingSettings = null;
    if (state.status) render(state.status);
  }
}

function chooseFace(style) {
  const face = FACES.find((item) => item.id === style);
  updateSettings({ clockStyle: style, page: "clock" }, `已切换到「${face?.name || style}」，板子下次轮询时生效`);
}

// ---------------------------------------------------------------- studio

const studio = {
  painter: null,
  processor: null,
  previewBusy: false,
  previewAgain: false
};

// Output size and the part of the video it shows (rect, in video pixels).
function currentLayout() {
  const preset = SIZE_PRESETS[$("#sizeSelect").value] || SIZE_PRESETS.full;
  const meta = state.videoMeta || { width: 4, height: 3 };
  const trim = $("#trimBars").checked && state.borders?.url === state.fileUrl;
  return frameLayout(meta.width, meta.height, {
    content: trim ? state.borders.box : null,
    framing: $("#framingSelect").value,
    maxWidth: preset.maxWidth,
    maxHeight: preset.maxHeight
  });
}

// Runs once per loaded video; the preview and estimate update when it is done.
function findBorders() {
  const url = state.fileUrl;
  const job = { url, busy: true, done: null, box: null };
  state.borders = job;
  job.done = detectBorders(url, {
    onProgress: (done, total) => {
      if (state.borders === job) $("#bordersInfo").textContent = `检测黑边 ${done}/${total}…`;
    }
  })
    .then((box) => {
      job.box = box;
    })
    .catch(() => {
      job.box = null; // the picture is used as it is
    })
    .finally(() => {
      job.busy = false;
      if (state.borders !== job) return;
      updateEstimate();
      updatePreview();
    });
  updateEstimate();
}

function bitmapOptions() {
  return {
    mode: state.mode,
    threshold: Number($("#threshold").value),
    invert: $("#invert").checked
  };
}

function clipRange() {
  const duration = state.videoMeta?.duration || 0;
  let start = Math.max(0, Number($("#clipStart").value) || 0);
  let end = Number($("#clipEnd").value);
  if (!Number.isFinite(end) || end <= 0 || end > duration) end = duration;
  if (start >= end) start = 0;
  return { start, end };
}

async function loadFile(file) {
  if (!file) return;
  if (!file.type.startsWith("video/") && !/\.(mp4|m4v|mov|webm|mkv)$/i.test(file.name)) {
    toast("请选择一个视频文件", "error");
    return;
  }
  if (state.converting) return;
  stopSound();
  const video = $("#srcVideo");
  try {
    if (state.fileUrl) URL.revokeObjectURL(state.fileUrl);
    const meta = await loadVideo(video, file);
    state.file = file;
    state.fileUrl = meta.url;
    state.videoMeta = meta;
  } catch (error) {
    toast(error.message, "error");
    return;
  }
  $("#drop").hidden = true;
  $("#studio").hidden = false;
  $("#fileName").textContent = file.name;
  $("#fileMeta").textContent = `${state.videoMeta.width}×${state.videoMeta.height} · ${formatDuration(state.videoMeta.duration)} · ${formatBytes(file.size)}`;
  $("#clipStart").value = "0";
  $("#clipEnd").value = String(Math.floor(state.videoMeta.duration * 10) / 10);
  $("#clipEnd").max = String(state.videoMeta.duration);
  $("#clipStart").max = String(state.videoMeta.duration);
  if (!$("#animName").value) $("#animName").value = suggestAnimationId(file.name);
  $("#animName").placeholder = suggestAnimationId(file.name);
  $("#soundToggleWrap").title = "";
  $("#soundToggle").disabled = false;
  findBorders();
  await updatePreview();
}

async function updatePreview() {
  // The same video element plays the sound during board playback.
  if (!state.videoMeta || state.converting || soundSync?.active) return;
  if (studio.previewBusy) {
    studio.previewAgain = true;
    return;
  }
  studio.previewBusy = true;
  try {
    do {
      studio.previewAgain = false;
      const { width, height, rect } = currentLayout();
      if (!studio.processor || studio.processor.width !== width || studio.processor.height !== height) {
        studio.processor = new FrameProcessor(width, height);
      }
      const { start, end } = clipRange();
      const fraction = Number($("#scrub").value) / 1000;
      const video = $("#srcVideo");
      const target = Math.min(end - 0.01, start + fraction * (end - start));
      if (Math.abs(video.currentTime - target) > 0.001) await seekTo(video, Math.max(0, target));
      studio.painter.draw(studio.processor.bitmapFrom(video, bitmapOptions(), rect), width, height);
    } while (studio.previewAgain);
  } catch (error) {
    toast(error.message, "error");
  } finally {
    studio.previewBusy = false;
  }
}

function updateEstimate() {
  if (!state.videoMeta) return;
  renderBordersInfo();
  const { width, height } = currentLayout();
  const { start, end } = clipRange();
  const fps = Number($("#fpsSelect").value);
  const frames = Math.max(1, Math.floor((end - start) * fps));
  const method = $("#precise").checked || !supportsFrameCallbacks() ? "逐帧跳转（较慢）" : `边播边取（约 ${Math.max(1, Math.min(4, Math.floor(45 / fps)))} 倍速）`;
  const scale = Math.max(1, Math.min(Math.floor(400 / width), Math.floor(300 / height)));
  $("#estimate").textContent =
    `${frames} 帧 · ${width}×${height}${scale > 1 ? `（板上放大 ${scale} 倍）` : ""} · ${fps} fps · ${formatDuration(end - start)} · ${method}`;
  $("#thresholdField").hidden = state.mode !== "threshold";
}

function renderBordersInfo() {
  const node = $("#bordersInfo");
  const job = state.borders;
  if (!job || job.url !== state.fileUrl) node.textContent = "";
  else if (job.busy) node.textContent = node.textContent || "检测黑边…";
  else if (!job.box) node.textContent = "没有黑边";
  else {
    const where = describeTrim(job.box, state.videoMeta.width, state.videoMeta.height);
    node.textContent = $("#trimBars").checked ? `已去掉黑边（${where}）` : `有黑边（${where}）`;
  }
}

async function convertAndUpload() {
  if (!state.videoMeta || state.converting) return;
  const id = sanitizeAnimationId($("#animName").value || $("#animName").placeholder);
  if (!id) {
    toast("动画名只能用小写字母、数字、- 和 _", "error");
    $("#animName").focus();
    return;
  }
  $("#animName").value = id;
  stopSound(); // the converter needs the video element; the board keeps playing
  const controller = new AbortController();
  state.converting = controller;
  const video = $("#srcVideo");
  const precise = $("#precise").checked;
  setStudioBusy(true, !precise && supportsFrameCallbacks());
  if (state.borders?.busy) {
    $("#progressText").textContent = "等黑边检测完…";
    await state.borders.done;
  }
  const { width, height, rect } = currentLayout();
  const { start, end } = clipRange();
  const fps = Number($("#fpsSelect").value);
  const began = performance.now();
  let lastDraw = 0;
  try {
    const result = await convertVideo(video, {
      width,
      height,
      rect,
      fps,
      start,
      end,
      precise,
      bitmap: bitmapOptions(),
      signal: controller.signal,
      onFrame: (bitmap) => {
        const now = performance.now();
        if (now - lastDraw > 90) {
          lastDraw = now;
          studio.painter.draw(bitmap, width, height);
        }
      },
      onProgress: ({ done, total, bytes }) => {
        const elapsed = (performance.now() - began) / 1000;
        const speed = elapsed > 0 ? (done / fps / elapsed).toFixed(1) : "0";
        $("#progressBar").style.width = `${((done / total) * 100).toFixed(1)}%`;
        $("#progressText").textContent = `${done}/${total} 帧 · ${formatBytes(bytes)} · ${speed}×`;
      }
    });
    $("#progressText").textContent = `上传 ${formatBytes(result.bytes.length)}…`;
    const saved = await api(`/anim/${encodeURIComponent(id)}`, { method: "PUT", body: result.bytes });
    rememberClip(id, { clipStart: start, fileName: state.file.name, fileSize: state.file.size });
    state.lastConverted = { id, clipStart: start };
    const ratio = result.frames * ((width + 7) >> 3) * height;
    toast(`已上传 ${id}：${saved.animation.frames} 帧，${formatBytes(result.bytes.length)}（压缩到 ${((result.bytes.length / ratio) * 100).toFixed(1)}%）`);
    await refresh();
  } catch (error) {
    if (error.name === "AbortError") toast("已取消转换");
    else toast(`转换失败：${error.message}`, "error");
  } finally {
    state.converting = null;
    setStudioBusy(false, false);
    video.playbackRate = 1;
    updatePreview();
  }
}

function setStudioBusy(busy, playbackMode) {
  for (const node of document.querySelectorAll("#studio select, #studio input, #changeFile, #modeButtons button")) {
    node.disabled = busy;
  }
  $("#convertBtn").hidden = busy;
  $("#cancelBtn").hidden = !busy;
  $("#progress").hidden = !busy;
  $("#keepVisible").hidden = !playbackMode;
  if (busy) {
    $("#progressBar").style.width = "0%";
    $("#progressText").textContent = "准备中…";
  }
}

function rememberClip(id, clip) {
  const clips = recall(STORAGE_CLIPS, true) || {};
  clips[id] = clip;
  store(STORAGE_CLIPS, clips);
}

function clipFor(id) {
  if (state.lastConverted?.id === id) return state.lastConverted;
  const clip = (recall(STORAGE_CLIPS, true) || {})[id];
  if (clip && state.file && clip.fileName === state.file.name && clip.fileSize === state.file.size) return clip;
  return null;
}

// ---------------------------------------------------------------- library

function renderLibrary(status) {
  const animations = status.animations || [];
  const defaultId = status.settings?.defaultAnimation || "";
  $("#animEmpty").hidden = animations.length > 0;
  $("#animList").replaceChildren(
    ...animations.map((anim) => {
      const explicitDefault = defaultId === anim.id;
      const implicitDefault = !defaultId && anim === animations[0];
      const armed = state.deleteArmed === anim.id;
      return el(
        "li",
        { class: "anim" },
        el(
          "div",
          { class: "anim-main" },
          el(
            "div",
            { class: "anim-id" },
            anim.id,
            explicitDefault || implicitDefault
              ? el("span", {
                  class: "default-badge",
                  text: explicitDefault ? "默认" : "默认 · 自动",
                  title: explicitDefault ? "" : "没有指定默认动画时，秘技播放列表里的第一个"
                })
              : null
          ),
          el("div", {
            class: "anim-sub",
            text: `${anim.width}×${anim.height} · ${(anim.fpsX100 / 100).toFixed(anim.fpsX100 % 100 ? 2 : 0)} fps · ${formatDuration(anim.durationS)} · ${formatBytes(anim.bytes)}`
          })
        ),
        el(
          "div",
          { class: "anim-actions" },
          el("button", { type: "button", class: "primary small", onclick: () => playOnBoard(anim.id), text: "▶ 板子播放" }),
          el("button", { type: "button", class: "ghost small", onclick: () => previewLocally(anim.id), text: "预览" }),
          defaultId === anim.id
            ? el("button", { type: "button", class: "ghost small", onclick: () => updateSettings({ defaultAnimation: "" }, "已取消默认"), text: "取消默认" })
            : el("button", { type: "button", class: "ghost small", onclick: () => updateSettings({ defaultAnimation: anim.id }, `秘技现在播放 ${anim.id}`), text: "设为默认" }),
          el("button", { type: "button", class: "danger small", onclick: () => deleteAnimation(anim.id), text: armed ? "确认删除" : "删除" })
        )
      );
    })
  );
  const egg = status.egg || {};
  $("#stopEgg").disabled = !egg.playing && mirror.mode === "idle";
  if (!egg.playing) $("#eggPhase").textContent = "";
  const soundAvailable = Boolean(state.file);
  $("#soundToggle").disabled = !soundAvailable;
  $("#soundToggleWrap").title = soundAvailable ? "" : "先在彩蛋工坊里载入同一个本地视频";
}

let deleteTimer = 0;
async function deleteAnimation(id) {
  if (state.deleteArmed !== id) {
    state.deleteArmed = id;
    clearTimeout(deleteTimer);
    deleteTimer = setTimeout(() => {
      state.deleteArmed = "";
      if (state.status) renderLibrary(state.status);
    }, 3000);
    if (state.status) renderLibrary(state.status);
    return;
  }
  state.deleteArmed = "";
  try {
    await api(`/anim/${encodeURIComponent(id)}`, { method: "DELETE" });
    toast(`已删除 ${id}`);
    await refresh();
  } catch (error) {
    toast(`删除失败：${error.message}`, "error");
  }
}

async function playOnBoard(id) {
  const wantSound = $("#soundToggle").checked && Boolean(state.file) && !state.converting;
  if (wantSound) unlockSound($("#srcVideo"));
  state.eggEpoch += 1;
  try {
    const result = await api("/egg/play", { method: "POST", json: { id } });
    state.eggEpoch += 1; // a /status sent while this request was out is stale too
    const egg = result.egg;
    toast(`板子将在 ${Math.max(1, Math.round((egg.startAtMs - serverNow()) / 1000))} 秒后开始播放 ${id}`);
    startMirror({ id, startServerMs: egg.startAtMs, board: true, rev: egg.rev });
    if (wantSound) {
      const clip = clipFor(id);
      if (!clip) toast("没找到这个动画对应的片段起点，声音从视频开头播放");
      startSound(egg, clip?.clipStart || 0);
    }
    refresh();
  } catch (error) {
    if (wantSound && !soundSync.active) $("#srcVideo").muted = true;
    toast(`没能播放：${error.message}`, "error");
  }
}

async function stopEgg() {
  stopSound();
  stopMirror();
  state.eggEpoch += 1;
  try {
    await api("/egg/stop", { method: "POST", json: {} });
  } catch (error) {
    toast(`停止失败：${error.message}`, "error");
  }
  state.eggEpoch += 1;
  refresh();
}

// ---------------------------------------------------------------- sound sync

let soundSync = null; // SoundSync on #srcVideo, created in boot()
const sound = { rev: 0 }; // the egg the sound belongs to

function audioOffsetMs() {
  return Number($("#audioOffset").value) || 0;
}

// Safari lets a page start sound later only from an element it played during
// a click. play() and pause() in the same task unlock it without a sound.
function unlockSound(video) {
  video.muted = false;
  try {
    video.play()?.catch?.(() => {});
  } catch {
    // older engines without a play() promise
  }
  video.pause();
}

function startSound(egg, clipStart) {
  const startServerMs = egg.startAtMs;
  const lengthS = Number.isFinite(egg.endsAtMs) ? Math.max(0, (egg.endsAtMs - egg.startAtMs) / 1000) : Infinity;
  sound.rev = egg.rev;
  soundSync.start({
    from: clipStart,
    to: clipStart + lengthS,
    // Read on every check: offset slider moves and clock updates apply while playing.
    position: (nowMs) => clipStart + (nowMs + serverClock.offsetMs - startServerMs - audioOffsetMs()) / 1000,
    onEnd: () => {
      sound.rev = 0;
      rememberSoundLatency();
    },
    onBlocked: () => {
      sound.rev = 0;
      toast("浏览器拦下了声音，点一下页面再试", "error");
    }
  });
}

function stopSound() {
  sound.rev = 0;
  if (!soundSync?.active) return;
  soundSync.stop();
  rememberSoundLatency();
}

// How long this browser and sound output take from play() to sound, so the
// next start (also after a reload) is in step from the first moment.
function rememberSoundLatency() {
  if (soundSync?.startLatencyMs > 0) store(STORAGE_LATENCY, String(Math.round(soundSync.startLatencyMs)));
}

// ---------------------------------------------------------------- mirror

const mirror = {
  painter: null,
  cache: null,
  mode: "idle",
  raf: 0,
  rev: 0,
  doneRev: 0, // a board egg this page already played to the end
  iterator: null,
  index: -1,
  frame: null,
  idleImage: null
};

async function animationBytes(id) {
  if (mirror.cache?.id === id) return mirror.cache;
  const response = await api(`/anim/${encodeURIComponent(id)}`, { raw: true });
  const bytes = new Uint8Array(await response.arrayBuffer());
  const { header } = parseRla(bytes);
  mirror.cache = { id, bytes, header };
  return mirror.cache;
}

function drawIdle() {
  const canvas = $("#mirrorCanvas");
  const context = canvas.getContext("2d");
  if (mirror.idleImage?.complete && mirror.idleImage.naturalWidth) {
    context.imageSmoothingEnabled = false;
    context.drawImage(mirror.idleImage, 0, 0, canvas.width, canvas.height);
  } else mirror.painter.clear();
  $("#mirrorLabel").textContent = "待机 · 内置彩蛋";
}

async function startMirror({ id, startServerMs, board, rev = 0 }) {
  cancelAnimationFrame(mirror.raf);
  mirror.mode = board ? "board" : "local";
  mirror.rev = rev;
  $("#stopEgg").disabled = false;
  let clip;
  try {
    clip = await animationBytes(id);
  } catch (error) {
    toast(`读不到动画：${error.message}`, "error");
    stopMirror();
    return;
  }
  if (mirror.rev !== rev || mirror.mode === "idle") return;
  const { bytes, header } = clip;
  const fps = header.fpsX100 / 100;
  const duration = header.frameCount / fps;
  const startLocal = board ? null : startServerMs;
  mirror.iterator = decodeFrames(bytes);
  mirror.index = -1;
  mirror.frame = null;
  const tick = () => {
    const now = board ? serverNow() : Date.now();
    const elapsed = (now - (board ? startServerMs : startLocal)) / 1000;
    if (elapsed < 0) {
      mirror.painter.text([String(Math.ceil(-elapsed)), id.toUpperCase()]);
      $("#mirrorLabel").textContent = board ? "板子倒计时" : "本地预览";
      if (board) $("#eggPhase").textContent = `倒计时 · ${id}`;
    } else {
      if (board) $("#eggPhase").textContent = `播放中 · ${id}`;
      const target = Math.floor(elapsed * fps);
      if (target >= header.frameCount) {
        // The bridge keeps reporting the egg for a few seconds after the end.
        if (board) mirror.doneRev = rev;
        stopMirror();
        return;
      }
      if (target < mirror.index) {
        mirror.iterator = decodeFrames(bytes);
        mirror.index = -1;
      }
      let advanced = false;
      while (mirror.index < target) {
        const next = mirror.iterator.next();
        if (next.done) break;
        mirror.frame = next.value;
        mirror.index += 1;
        advanced = true;
      }
      if (advanced && mirror.frame) mirror.painter.draw(mirror.frame, header.width, header.height);
      $("#mirrorLabel").textContent = `${board ? "板子播放中" : "本地预览"} · ${formatDuration(elapsed)} / ${formatDuration(duration)}`;
    }
    mirror.raf = requestAnimationFrame(tick);
  };
  tick();
}

function stopMirror() {
  cancelAnimationFrame(mirror.raf);
  mirror.raf = 0;
  mirror.mode = "idle";
  mirror.rev = 0;
  $("#eggPhase").textContent = "";
  drawIdle();
  if (state.status) $("#stopEgg").disabled = !state.status.egg?.playing;
}

function previewLocally(id) {
  stopSound();
  startMirror({ id, startServerMs: Date.now() + 600, board: false, rev: -Date.now() });
}

// Follows eggs started elsewhere (the board's secret gesture, the menu bar app).
function syncMirrorWithEgg(egg) {
  if (egg?.playing) {
    if (sound.rev && sound.rev !== egg.rev) stopSound(); // another egg took over
    if (mirror.doneRev === egg.rev) return;
    if (mirror.mode !== "board" || mirror.rev !== egg.rev) {
      startMirror({ id: egg.id, startServerMs: egg.startAtMs, board: true, rev: egg.rev });
    }
  } else {
    if (mirror.mode === "board") stopMirror();
    if (sound.rev) stopSound();
  }
}

// ---------------------------------------------------------------- wiring

function wire() {
  $("#tokenForm").addEventListener("submit", () => {
    const token = $("#tokenInput").value.trim();
    if (!token) return;
    state.token = token;
    store(STORAGE_TOKEN, token);
    schedule(0);
  });

  $("#pageButtons").addEventListener("click", (event) => {
    const button = event.target.closest("button[data-page]");
    if (!button) return;
    updateSettings({ page: button.dataset.page }, `板子将切到「${PAGE_NAMES[button.dataset.page]}」`);
  });
  $("#hour12Toggle").addEventListener("change", (event) => updateSettings({ hour12: event.target.checked }));
  $("#secondsToggle").addEventListener("change", (event) => updateSettings({ showSeconds: event.target.checked }));

  $("#tzForm").addEventListener("submit", (event) => {
    event.preventDefault();
    const value = $("#tzInput").value.trim();
    updateSettings({ tzOverride: value }, value ? "时区覆盖已保存" : "已恢复跟随系统时区");
  });
  $("#tzClear").addEventListener("click", () => {
    $("#tzInput").value = "";
    updateSettings({ tzOverride: "" }, "已恢复跟随系统时区");
  });

  const drop = $("#drop");
  $("#fileInput").addEventListener("change", (event) => loadFile(event.target.files?.[0]));
  for (const type of ["dragenter", "dragover"]) {
    drop.addEventListener(type, (event) => {
      event.preventDefault();
      drop.classList.add("dragover");
    });
  }
  for (const type of ["dragleave", "drop"]) {
    drop.addEventListener(type, () => drop.classList.remove("dragover"));
  }
  drop.addEventListener("drop", (event) => {
    event.preventDefault();
    loadFile(event.dataTransfer?.files?.[0]);
  });
  $("#changeFile").addEventListener("click", () => {
    $("#fileInput").value = "";
    $("#fileInput").click();
  });

  let previewTimer = 0;
  const schedulePreview = () => {
    updateEstimate();
    clearTimeout(previewTimer);
    previewTimer = setTimeout(updatePreview, 40);
  };
  for (const id of ["#sizeSelect", "#framingSelect", "#trimBars", "#fpsSelect", "#threshold", "#invert", "#clipStart", "#clipEnd", "#precise", "#scrub"]) {
    $(id).addEventListener("input", schedulePreview);
  }
  const framing = recall(STORAGE_FRAMING, true);
  if (framing?.mode === "fit" || framing?.mode === "fill") $("#framingSelect").value = framing.mode;
  if (typeof framing?.trim === "boolean") $("#trimBars").checked = framing.trim;
  for (const id of ["#framingSelect", "#trimBars"]) {
    $(id).addEventListener("change", () => store(STORAGE_FRAMING, { mode: $("#framingSelect").value, trim: $("#trimBars").checked }));
  }
  $("#threshold").addEventListener("input", (event) => {
    $("#thresholdValue").textContent = event.target.value;
  });
  $("#modeButtons").addEventListener("click", (event) => {
    const button = event.target.closest("button[data-mode]");
    if (!button) return;
    state.mode = button.dataset.mode;
    for (const node of document.querySelectorAll("#modeButtons button")) {
      node.setAttribute("aria-pressed", String(node === button));
    }
    schedulePreview();
  });
  $("#convertBtn").addEventListener("click", convertAndUpload);
  $("#cancelBtn").addEventListener("click", () => state.converting?.abort());

  $("#stopEgg").addEventListener("click", stopEgg);
  $("#soundToggle").addEventListener("change", (event) => {
    if (!event.target.checked) stopSound();
  });
  const offset = recall(STORAGE_OFFSET);
  if (offset !== null) $("#audioOffset").value = offset;
  const showOffset = () => {
    $("#offsetValue").textContent = `${$("#audioOffset").value} ms`;
  };
  showOffset();
  $("#audioOffset").addEventListener("input", () => {
    showOffset();
    store(STORAGE_OFFSET, $("#audioOffset").value);
  });
}

function boot() {
  initToken();
  studio.painter = new PanelPainter($("#bitCanvas"));
  studio.painter.clear();
  mirror.painter = new PanelPainter($("#mirrorCanvas"));
  mirror.idleImage = new Image();
  mirror.idleImage.onload = () => {
    if (mirror.mode === "idle") drawIdle();
  };
  mirror.idleImage.src = "previews/egg-builtin.png";
  drawIdle();
  $("#srcVideo").muted = true;
  soundSync = new SoundSync($("#srcVideo"));
  const latency = Number(recall(STORAGE_LATENCY));
  if (latency > 0 && latency <= soundSync.options.maxStartLatencyMs) soundSync.startLatencyMs = latency;
  buildFaces();
  wire();
  updateEstimate();
  refresh().then(() => schedule());
}

boot();

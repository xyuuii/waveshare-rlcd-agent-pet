const DEFAULT_REFRESH_MS = 90_000;

export function buildHermesLogSyncNotifications(state = {}, activity = null, options = {}) {
  const now = Number.isFinite(options.nowMs) ? options.nowMs : Date.now();
  const refreshMs = positiveNumber(options.refreshMs, DEFAULT_REFRESH_MS);
  const notifications = [];

  if (!activity) {
    if (state.hermesActive) {
      notifications.push(buildNotifyArgs({
        task: state.hermesLastTask || "hermes-session",
        status: "completed",
        message: "Hermes response stopped",
        sessionId: state.hermesLastSessionId || "",
        notify: false
      }));
      state.hermesActive = false;
      state.hermesLastRunningNotify = 0;
    }
    return { notifications, state };
  }

  const task = activity.task || state.hermesLastTask || "hermes-session";
  const sessionId = activity.sessionId || state.hermesLastSessionId || "";

  if (activity.active) {
    const wasActive = Boolean(state.hermesActive);
    const lastNotify = Number(state.hermesLastRunningNotify || 0);
    if (!wasActive || now - lastNotify >= refreshMs) {
      notifications.push(buildNotifyArgs({
        task,
        status: "thinking",
        message: "Hermes is thinking",
        sessionId,
        notify: false
      }));
      state.hermesLastRunningNotify = now;
    }
    state.hermesActive = true;
    state.hermesLastSeen = now;
    state.hermesLastTask = task;
    state.hermesLastSessionId = sessionId;
    return { notifications, state };
  }

  if (activity.completed) {
    const completedKey = activity.completedKey || `${sessionId}:${activity.endedAtMs || ""}`;
    const lastCompletedNotify = Number(state.hermesLastCompletedNotify || 0);
    const shouldRefreshCompleted = !lastCompletedNotify || now - lastCompletedNotify >= refreshMs;
    if (completedKey && (state.hermesLastCompletedKey !== completedKey || shouldRefreshCompleted)) {
      notifications.push(buildNotifyArgs({
        task,
        status: "completed",
        message: "Hermes finished the turn",
        sessionId,
        notify: false
      }));
      state.hermesLastCompletedKey = completedKey;
      state.hermesLastCompletedNotify = now;
    }
    state.hermesActive = false;
    state.hermesLastRunningNotify = 0;
    state.hermesLastSeen = now;
    state.hermesLastTask = task;
    state.hermesLastSessionId = sessionId;
  }

  return { notifications, state };
}

function buildNotifyArgs({ task, status, message, sessionId, notify }) {
  const args = [
    "--source", "hermes",
    "--task", task,
    "--status", status,
    "--message", message
  ];
  if (sessionId) args.push("--session-id", sessionId);
  args.push(notify ? "--notify" : "--no-notify");
  return args;
}

function positiveNumber(value, fallback) {
  return Number.isFinite(value) && value > 0 ? value : fallback;
}

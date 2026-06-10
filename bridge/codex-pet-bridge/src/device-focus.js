import { createHash } from "node:crypto";

const ACTIVE_STATUSES = new Set([
  "running",
  "working",
  "thinking",
  "searching",
  "tool-use",
  "started",
  "progress",
  "near-complete"
]);

function buildStableSlotKey(item) {
  if (item.sessionId) {
    return JSON.stringify([item.source ?? null, "session", item.sessionId]);
  }
  if (item.workspace) {
    return JSON.stringify([item.source ?? null, "workspace", item.workspace]);
  }
  return JSON.stringify([item.source ?? null, "task", item.task ?? null]);
}

function buildSlotAliasIds(item) {
  if (item.sessionId) {
    return [buildSlotId(JSON.stringify([item.source ?? null, "session", item.sessionId]))];
  }
  if (item.workspace) {
    return [buildSlotId(JSON.stringify([item.source ?? null, "workspace", item.workspace]))];
  }
  return [buildSlotId(buildStableSlotKey(item))];
}

function buildSlotId(slotKey) {
  return `slot_${createHash("sha256").update(slotKey).digest("hex").slice(0, 24)}`;
}

function itemTime(item) {
  return item?.time || item?.updated_at || "";
}

function itemTimeValue(item) {
  return Date.parse(itemTime(item) || 0);
}

function hasSlotIdentity(item) {
  return Boolean(item?.source || item?.task || item?.sessionId || item?.workspace);
}

function hasStrongIdentity(item) {
  return Boolean(item?.sessionId || item?.workspace);
}

function priorityForStatus(status) {
  if (status === "needs-attention") return 0;
  if (ACTIVE_STATUSES.has(status)) return 1;
  if (status === "completed") return 2;
  if (status === "error") return 3;
  return 4;
}

export function sortVisibleAgentSlots(slots) {
  return [...slots].sort((left, right) => {
    const priority = priorityForStatus(left.status) - priorityForStatus(right.status);
    if (priority !== 0) return priority;
    return Date.parse(right.updated_at || 0) - Date.parse(left.updated_at || 0);
  });
}

function createSlot(item) {
  const id = buildSlotId(buildStableSlotKey(item));
  return {
    id,
    source: item.source || "",
    task: item.task || "",
    status: item.status || "idle",
    updated_at: itemTime(item) || "",
    sessionId: item.sessionId || "",
    workspace: item.workspace || "",
    aliasIds: [...new Set([id, ...buildSlotAliasIds(item)])]
  };
}

function slotMatchScore(slot, item) {
  if ((slot.source || "") !== (item.source || "")) return -1;

  if (slot.sessionId && item.sessionId) {
    return slot.sessionId === item.sessionId ? 4 : -1;
  }

  if (slot.workspace && item.workspace) {
    return slot.workspace === item.workspace ? 3 : -1;
  }

  if (slot.task && item.task && slot.task === item.task) {
    if (slot.sessionId && item.sessionId && slot.sessionId !== item.sessionId) return -1;
    if (slot.workspace && item.workspace && slot.workspace !== item.workspace) return -1;
    return 1;
  }

  return -1;
}

function findMatchingSlot(slots, item) {
  let bestSlot = null;
  let bestScore = -1;

  for (const slot of slots) {
    const score = slotMatchScore(slot, item);
    if (score < 0) continue;
    if (score > bestScore) {
      bestSlot = slot;
      bestScore = score;
      continue;
    }
    if (score === bestScore && Date.parse(slot.updated_at || 0) > Date.parse(bestSlot?.updated_at || 0)) {
      bestSlot = slot;
    }
  }

  return bestSlot;
}

function mergeSlot(slot, item) {
  const slotWasStrong = hasStrongIdentity(slot);
  if (item.sessionId) slot.sessionId = item.sessionId;
  if (item.workspace) slot.workspace = item.workspace;
  slot.id = buildSlotId(buildStableSlotKey(slot));
  if (hasStrongIdentity(item) || !slotWasStrong) {
    slot.aliasIds = [...new Set([slot.id, ...(slot.aliasIds || []), ...buildSlotAliasIds(item)])];
  } else {
    slot.aliasIds = [...new Set([slot.id, ...(slot.aliasIds || [])])];
  }

  if (!slot.task && item.task) {
    slot.task = item.task;
  }

  if (itemTimeValue(item) >= Date.parse(slot.updated_at || 0)) {
    slot.source = item.source || slot.source;
    slot.task = item.task || slot.task;
    slot.status = item.status || slot.status;
    slot.updated_at = itemTime(item) || slot.updated_at;
  }
}

export function buildVisibleAgentSlots(events, current = null) {
  const slots = [];
  const candidates = current ? [...events, current] : [...events];

  for (const item of candidates) {
    if (!hasSlotIdentity(item)) continue;
    const slot = findMatchingSlot(slots, item);
    if (slot) {
      mergeSlot(slot, item);
      continue;
    }
    slots.push(createSlot(item));
  }

  return sortVisibleAgentSlots(slots);
}

export function resolveFocusSelection({ focus, slots, autoCurrent }) {
  if (!focus || focus === "auto") {
    return {
      mode: "auto",
      current: autoCurrent,
      focusId: "",
      focusIndex: -1,
      focusCount: slots.length
    };
  }

  const focusIndex = slots.findIndex((slot) => slot.id === focus);
  const aliasedFocusIndex = focusIndex >= 0
    ? focusIndex
    : slots.findIndex((slot) => Array.isArray(slot.aliasIds) && slot.aliasIds.includes(focus));
  if (aliasedFocusIndex < 0) {
    return {
      mode: "auto",
      current: autoCurrent,
      focusId: "",
      focusIndex: -1,
      focusCount: slots.length
    };
  }

  return {
    mode: "pinned",
    current: slots[aliasedFocusIndex],
    focusId: slots[aliasedFocusIndex].id,
    focusIndex: aliasedFocusIndex,
    focusCount: slots.length
  };
}

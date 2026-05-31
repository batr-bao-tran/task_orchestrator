import type { WorkflowScheduleMode } from "./types";

const MINUTE_IN_MS = 60_000;
const ABSOLUTE_TIME_FLOOR_MS = Date.UTC(2000, 0, 1);

export interface WorkflowScheduleContext {
  mode: WorkflowScheduleMode;
  anchorMs?: number;
}

export function looksLikeAbsoluteTimestamp(value: number): boolean {
  return value >= ABSOLUTE_TIME_FLOOR_MS;
}

export function normalizeScheduleAnchorMs(value: number): number {
  const resolvedValue = value > 0 ? value : Date.now();
  return Math.floor(resolvedValue / MINUTE_IN_MS) * MINUTE_IN_MS;
}

export function toDisplayTimestamp(
  rawValue: number,
  context: WorkflowScheduleContext,
  options?: { zeroMeansUnset?: boolean },
): number {
  if (context.mode === "absolute_ms") {
    return rawValue;
  }

  if (options?.zeroMeansUnset === true && rawValue <= 0) {
    return 0;
  }

  return (context.anchorMs ?? 0) + rawValue * MINUTE_IN_MS;
}

export function toDisplayDurationMs(rawValue: number, context: WorkflowScheduleContext): number {
  return context.mode === "absolute_ms" ? rawValue : rawValue * MINUTE_IN_MS;
}

export function fromDisplayTimestamp(
  displayValue: number,
  context: WorkflowScheduleContext,
  options?: { zeroMeansUnset?: boolean },
): number {
  if (context.mode === "absolute_ms") {
    return displayValue;
  }

  if (options?.zeroMeansUnset === true && displayValue <= 0) {
    return 0;
  }

  return Math.max(0, Math.round((displayValue - (context.anchorMs ?? 0)) / MINUTE_IN_MS));
}

export function fromDisplayDurationMs(displayValue: number, context: WorkflowScheduleContext): number {
  return context.mode === "absolute_ms" ? displayValue : Math.max(0, Math.round(displayValue / MINUTE_IN_MS));
}

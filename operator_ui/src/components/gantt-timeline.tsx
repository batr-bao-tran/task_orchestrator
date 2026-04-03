import { type WheelEvent, useCallback, useEffect, useRef, useState } from "react";
import { createPortal } from "react-dom";
import type { WorkflowAssignment, WorkflowActor, WorkflowTask, WorkflowState } from "../lib/types";

interface GanttTimelineProps {
  assignments: WorkflowAssignment[];
  actors: WorkflowActor[];
  tasks: WorkflowTask[];
  workflowState: WorkflowState;
}

interface TooltipState {
  assignment: WorkflowAssignment;
  task: WorkflowTask | undefined;
  /** Position relative to the viewport (fixed positioning). */
  x: number;
  y: number;
  pinned: boolean;
}

interface DaySegment {
  start: number;
  end: number;
  label: string;
}

interface TimeRange {
  rangeStart: number;
  rangeEnd: number;
}

const ROW_HEIGHT = 40;
const BAR_PADDING_Y = 6;
const MAJOR_AXIS_HEIGHT = 24;
const TOP_MARGIN = 64;
const BOTTOM_MARGIN = 12;
const LEFT_MARGIN = 120;
const LEFT_MARGIN_NARROW = 80;
const RIGHT_MARGIN = 16;
const BAR_RADIUS = 8;
const MIN_BAR_WIDTH_PX = 4;
const TOOLTIP_WIDTH_PX = 280;
const TOOLTIP_HEIGHT_PX = 220;
const TOOLTIP_VIEWPORT_PADDING = 16;

const MINUTE = 60_000;
const HOUR = 60 * MINUTE;
const DAY = 24 * HOUR;
const ZOOM_LEVELS_MS = [30 * MINUTE, HOUR, 3 * HOUR, 6 * HOUR, 12 * HOUR, DAY, 3 * DAY, 7 * DAY];

interface BarPalette {
  fill: string;
  stroke: string;
  text: string;
}

function barPalette(state: WorkflowState): BarPalette {
  switch (state) {
    case "planned":
    case "recovering":
      return { fill: "rgba(15, 122, 114, 0.22)", stroke: "rgba(15, 122, 114, 0.55)", text: "#0f7a72" };
    case "paused":
    case "planning":
    case "submitted":
      return { fill: "rgba(204, 107, 31, 0.22)", stroke: "rgba(204, 107, 31, 0.55)", text: "#cc6b1f" };
    case "cancelled":
    case "failed":
      return { fill: "rgba(175, 65, 49, 0.22)", stroke: "rgba(175, 65, 49, 0.55)", text: "#af4131" };
    default:
      return { fill: "rgba(15, 122, 114, 0.22)", stroke: "rgba(15, 122, 114, 0.55)", text: "#0f7a72" };
  }
}

function computeTickInterval(rangeMs: number): number {
  if (rangeMs <= 20 * MINUTE) return 5 * MINUTE;
  if (rangeMs <= 60 * MINUTE) return 10 * MINUTE;
  if (rangeMs <= 3 * HOUR) return 15 * MINUTE;
  if (rangeMs <= 6 * HOUR) return 30 * MINUTE;
  if (rangeMs <= 18 * HOUR) return HOUR;
  if (rangeMs <= 72 * HOUR) return 4 * HOUR;
  return 12 * HOUR;
}

function computeTicks(rangeStart: number, rangeEnd: number, interval: number): number[] {
  const first = Math.ceil(rangeStart / interval) * interval;
  const ticks: number[] = [];
  for (let t = first; t <= rangeEnd; t += interval) {
    ticks.push(t);
  }
  return ticks;
}

const tickTimeOnly = new Intl.DateTimeFormat("en-GB", { hour: "2-digit", minute: "2-digit" });
const tickDateShort = new Intl.DateTimeFormat("en-GB", { day: "2-digit", month: "short", hour: "2-digit", minute: "2-digit" });
const dayFormatter = new Intl.DateTimeFormat("en-GB", { weekday: "short", day: "2-digit", month: "short" });

/** Returns true if `ms` falls exactly on a midnight boundary (local time). */
function isMidnight(ms: number): boolean {
  const d = new Date(ms);
  return d.getHours() === 0 && d.getMinutes() === 0 && d.getSeconds() === 0;
}

/** Returns true if `ms` falls exactly on an hour boundary (local time). */
function isHourBoundary(ms: number): boolean {
  const d = new Date(ms);
  return d.getMinutes() === 0 && d.getSeconds() === 0;
}

/**
 * Adaptive tick label: shows date+time at midnight boundaries and at the first tick,
 * otherwise shows just the time. For multi-day ranges all ticks get date context.
 */
function formatTickLabel(ms: number, index: number, rangeMs: number): string {
  if (rangeMs > 2 * DAY) {
    return tickDateShort.format(new Date(ms));
  }
  if (index === 0 || isMidnight(ms)) {
    return tickDateShort.format(new Date(ms));
  }
  return tickTimeOnly.format(new Date(ms));
}

function formatDayLabel(ms: number): string {
  return dayFormatter.format(new Date(ms));
}

/** Format a timestamp for tooltip display (always includes date). */
function formatTooltipTime(ms: number): string {
  return tickDateShort.format(new Date(ms));
}

/** Short human-readable duration for bar labels. */
function formatDurationShort(ms: number): string {
  const totalMinutes = Math.round(ms / MINUTE);
  if (totalMinutes < 60) return `${String(totalMinutes)}m`;
  const h = Math.floor(totalMinutes / 60);
  const m = totalMinutes % 60;
  return m > 0 ? `${String(h)}h${String(m)}m` : `${String(h)}h`;
}

function startOfDay(ms: number): number {
  const day = new Date(ms);
  day.setHours(0, 0, 0, 0);
  return day.getTime();
}

function startOfNextDay(ms: number): number {
  const day = new Date(ms);
  day.setHours(0, 0, 0, 0);
  day.setDate(day.getDate() + 1);
  return day.getTime();
}

function isSameDay(first: number, second: number): boolean {
  const firstDay = new Date(first);
  const secondDay = new Date(second);
  return firstDay.getFullYear() === secondDay.getFullYear()
    && firstDay.getMonth() === secondDay.getMonth()
    && firstDay.getDate() === secondDay.getDate();
}

function formatVisibleRange(rangeStart: number, rangeEnd: number): string {
  const start = new Date(rangeStart);
  const end = new Date(rangeEnd);
  if (isSameDay(rangeStart, rangeEnd)) {
    return `${dayFormatter.format(start)}, ${tickTimeOnly.format(start)} to ${tickTimeOnly.format(end)}`;
  }

  return `${dayFormatter.format(start)} ${tickTimeOnly.format(start)} to ${dayFormatter.format(end)} ${tickTimeOnly.format(end)}`;
}

function computeDefaultTimeRange(referenceMs: number): TimeRange {
  return {
    rangeStart: startOfDay(referenceMs),
    rangeEnd: startOfNextDay(referenceMs),
  };
}

function rangesEqual(first: TimeRange, second: TimeRange): boolean {
  return first.rangeStart === second.rangeStart && first.rangeEnd === second.rangeEnd;
}

function createAnchoredRange(anchorMs: number, anchorRatio: number, durationMs: number): TimeRange {
  const clampedAnchorRatio = clamp(anchorRatio, 0, 1);
  const rangeStart = Math.round(anchorMs - clampedAnchorRatio * durationMs);
  return {
    rangeStart,
    rangeEnd: rangeStart + durationMs,
  };
}

function nextZoomDuration(rangeMs: number, direction: "in" | "out"): number | undefined {
  if (direction === "in") {
    const smallerLevels = [...ZOOM_LEVELS_MS].reverse();
    return smallerLevels.find((level) => level < rangeMs - MINUTE);
  }

  return ZOOM_LEVELS_MS.find((level) => level > rangeMs + MINUTE);
}

function formatWindowLabel(rangeStart: number, rangeEnd: number, referenceMs: number): string {
  if (rangesEqual({ rangeStart, rangeEnd }, computeDefaultTimeRange(referenceMs))) {
    return "Today";
  }

  const rangeMs = rangeEnd - rangeStart;
  const minutes = Math.round(rangeMs / MINUTE);
  if (minutes < 60) {
    return `${String(minutes)} min window`;
  }

  const hours = rangeMs / HOUR;
  if (hours < 24) {
    const roundedHours = hours >= 10 ? Math.round(hours) : Math.round(hours * 10) / 10;
    return `${Number.isInteger(roundedHours) ? String(roundedHours) : roundedHours.toFixed(1)}h window`;
  }

  const days = rangeMs / DAY;
  const roundedDays = days >= 10 ? Math.round(days) : Math.round(days * 10) / 10;
  return `${Number.isInteger(roundedDays) ? String(roundedDays) : roundedDays.toFixed(1)} day window`;
}

function computeDaySegments(rangeStart: number, rangeEnd: number): DaySegment[] {
  const segments: DaySegment[] = [];
  let cursor = startOfDay(rangeStart);

  while (cursor < rangeEnd) {
    const next = startOfNextDay(cursor);
    segments.push({
      start: Math.max(cursor, rangeStart),
      end: Math.min(next, rangeEnd),
      label: formatDayLabel(Math.max(cursor, rangeStart)),
    });
    cursor = next;
  }

  return segments;
}

function clamp(value: number, min: number, max: number): number {
  return Math.min(Math.max(value, min), max);
}

function clampTooltipPosition(x: number, y: number): { x: number; y: number } {
  if (typeof window === "undefined") {
    return { x, y };
  }

  const maxX = Math.max(TOOLTIP_VIEWPORT_PADDING, window.innerWidth - TOOLTIP_WIDTH_PX - TOOLTIP_VIEWPORT_PADDING);
  const maxY = Math.max(TOOLTIP_VIEWPORT_PADDING, window.innerHeight - TOOLTIP_HEIGHT_PX - TOOLTIP_VIEWPORT_PADDING);
  return {
    x: clamp(x, TOOLTIP_VIEWPORT_PADDING, maxX),
    y: clamp(y, TOOLTIP_VIEWPORT_PADDING, maxY),
  };
}

function isSameAssignment(first: WorkflowAssignment, second: WorkflowAssignment): boolean {
  return first.taskId === second.taskId
    && first.actorId === second.actorId
    && first.startTimeMs === second.startTimeMs
    && first.endTimeMs === second.endTimeMs;
}

function assignmentDomKey(assignment: WorkflowAssignment): string {
  return `${assignment.taskId}-${assignment.actorId}-${String(assignment.startTimeMs)}-${String(assignment.endTimeMs)}`;
}

function timeToX(timeMs: number, rangeStart: number, rangeEnd: number, chartWidth: number, leftMargin: number): number {
  const fraction = (timeMs - rangeStart) / (rangeEnd - rangeStart);
  return leftMargin + fraction * chartWidth;
}

export function GanttTimeline({ assignments, actors, tasks, workflowState }: GanttTimelineProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const [containerWidth, setContainerWidth] = useState(0);
  const [tooltip, setTooltip] = useState<TooltipState | undefined>();
  const [viewRange, setViewRange] = useState<TimeRange>(() => computeDefaultTimeRange(Date.now()));
  const rangeStart = viewRange.rangeStart;
  const rangeEnd = viewRange.rangeEnd;

  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;

    const observer = new ResizeObserver((entries) => {
      for (const entry of entries) {
        setContainerWidth(Math.floor(entry.contentRect.width));
      }
    });
    observer.observe(el);
    setContainerWidth(el.clientWidth);
    return () => { observer.disconnect(); };
  }, []);

  /** Dismiss a pinned tooltip when clicking outside the bars. */
  const handleBackgroundClick = useCallback(() => {
    setTooltip((prev) => (prev?.pinned === true ? undefined : prev));
  }, []);

  /** Dismiss pinned tooltip on any click outside the portal tooltip itself. */
  useEffect(() => {
    if (tooltip?.pinned !== true) return;
    const handler = () => { setTooltip(undefined); };
    window.addEventListener("click", handler);
    return () => { window.removeEventListener("click", handler); };
  }, [tooltip?.pinned]);

  const panBy = useCallback((direction: -1 | 1) => {
    setTooltip(undefined);
    setViewRange((prev) => {
      const rangeDuration = prev.rangeEnd - prev.rangeStart;
      const offset = Math.round(rangeDuration * 0.5) * direction;
      return {
        rangeStart: prev.rangeStart + offset,
        rangeEnd: prev.rangeEnd + offset,
      };
    });
  }, []);

  const zoomBy = useCallback((direction: "in" | "out", anchorRatio = 0.5) => {
    setTooltip(undefined);
    setViewRange((prev) => {
      const rangeDuration = prev.rangeEnd - prev.rangeStart;
      const nextDuration = nextZoomDuration(rangeDuration, direction);
      if (nextDuration === undefined) {
        return prev;
      }

      const anchorMs = prev.rangeStart + clamp(anchorRatio, 0, 1) * rangeDuration;
      return createAnchoredRange(anchorMs, anchorRatio, nextDuration);
    });
  }, []);

  const resetView = useCallback(() => {
    setTooltip(undefined);
    setViewRange(computeDefaultTimeRange(Date.now()));
  }, []);

  const createTooltipState = useCallback((
    assignment: WorkflowAssignment,
    task: WorkflowTask | undefined,
    clientX: number,
    clientY: number,
    pinned: boolean,
  ): TooltipState => {
    const position = clampTooltipPosition(clientX + 12, clientY - 8);
    return { assignment, task, x: position.x, y: position.y, pinned };
  }, []);

  const createBarTooltipState = useCallback((
    assignment: WorkflowAssignment,
    task: WorkflowTask | undefined,
    barX: number,
    barY: number,
    barWidth: number,
    barHeight: number,
    pinned: boolean,
  ): TooltipState => {
    const rect = containerRef.current?.getBoundingClientRect();
    const clientX = rect !== undefined ? rect.left + barX + barWidth / 2 : barX + barWidth / 2;
    const clientY = rect !== undefined ? rect.top + barY + barHeight / 2 : barY + barHeight / 2;
    return createTooltipState(assignment, task, clientX, clientY, pinned);
  }, [createTooltipState]);

  if (assignments.length === 0) {
    return (
      <div className="gantt-wrapper" ref={containerRef}>
        <div className="gantt-empty">
          <p className="muted">No assignments are scheduled for this plan.</p>
        </div>
      </div>
    );
  }

  if (containerWidth > 0 && containerWidth < 300) {
    return (
      <div className="gantt-wrapper" ref={containerRef}>
        <div className="gantt-empty">
          <p className="muted">Widen this panel to view the schedule timeline.</p>
        </div>
      </div>
    );
  }

  const actorRows = [...actors].sort((a, b) => a.id.localeCompare(b.id));
  const actorIndexMap = new Map<string, number>();
  actorRows.forEach((actor, index) => actorIndexMap.set(actor.id, index));

  const taskMap = new Map<string, WorkflowTask>();
  for (const task of tasks) {
    taskMap.set(task.id, task);
  }

  const effectiveLeftMargin = containerWidth < 500 ? LEFT_MARGIN_NARROW : LEFT_MARGIN;
  const chartAreaWidth = Math.max(0, containerWidth - effectiveLeftMargin - RIGHT_MARGIN);
  const chartHeight = TOP_MARGIN + actorRows.length * ROW_HEIGHT + BOTTOM_MARGIN;

  const nowMs = Date.now();
  const defaultRange = computeDefaultTimeRange(nowMs);
  const rangeDuration = rangeEnd - rangeStart;
  const atDefaultView = rangesEqual(viewRange, defaultRange);
  const tickInterval = computeTickInterval(rangeDuration);
  const ticks = computeTicks(rangeStart, rangeEnd, tickInterval);
  const daySegments = computeDaySegments(rangeStart, rangeEnd);
  const visibleRangeLabel = formatVisibleRange(rangeStart, rangeEnd);
  const visibleWindowLabel = formatWindowLabel(rangeStart, rangeEnd, nowMs);
  const actorLaneLabel = actorRows.length === 1 ? "1 actor" : `${String(actorRows.length)} actors`;

  const palette = barPalette(workflowState);

  const nowX = timeToX(nowMs, rangeStart, rangeEnd, chartAreaWidth, effectiveLeftMargin);
  const nowVisible = nowMs >= rangeStart && nowMs <= rangeEnd;

  const handleTimelineWheel = (event: WheelEvent<SVGSVGElement>) => {
    if (event.deltaY === 0 || chartAreaWidth <= 0) {
      return;
    }

    const direction = event.deltaY < 0 ? "in" : "out";
    const nextDuration = nextZoomDuration(rangeDuration, direction);
    if (nextDuration === undefined) {
      return;
    }

    event.preventDefault();
    const svgRect = event.currentTarget.getBoundingClientRect();
    const relativeX = clamp(event.clientX - svgRect.left - effectiveLeftMargin, 0, chartAreaWidth);
    const anchorRatio = chartAreaWidth === 0 ? 0.5 : relativeX / chartAreaWidth;
    zoomBy(direction, anchorRatio);
  };

  if (containerWidth === 0) {
    return <div className="gantt-wrapper" ref={containerRef} />;
  }

  return (
    <div className="gantt-wrapper" ref={containerRef} onClick={handleBackgroundClick}>
      <div className="gantt-toolbar">
        <div className="gantt-range">
          <p className="gantt-range-label">{visibleRangeLabel}</p>
          <p className="gantt-range-meta">{visibleWindowLabel} · {actorLaneLabel}</p>
        </div>
        <div className="gantt-toolbar-controls">
          <div className="gantt-nav" role="group" aria-label="Timeline navigation">
            <button
              type="button"
              className="gantt-nav-button"
              onClick={() => { panBy(-1); }}
            >
              Past
            </button>
            <button
              type="button"
              className="gantt-nav-button gantt-nav-button--reset"
              disabled={atDefaultView}
              onClick={resetView}
            >
              Reset
            </button>
            <button
              type="button"
              className="gantt-nav-button"
              onClick={() => { panBy(1); }}
            >
              Future
            </button>
          </div>
        </div>
        <p className="gantt-toolbar-hint">Scroll to zoom. Click or press Enter on a bar to pin details.</p>
      </div>

      <svg
        width={containerWidth}
        height={chartHeight}
        style={{ display: "block" }}
        onWheel={handleTimelineWheel}
        role="img"
        aria-label={`Schedule timeline for ${String(actorRows.length)} actors from ${visibleRangeLabel}`}
      >
        {/* Day header and boundaries */}
        <g>
          {daySegments.map((segment, index) => {
            const x = timeToX(segment.start, rangeStart, rangeEnd, chartAreaWidth, effectiveLeftMargin);
            const segmentEndX = timeToX(segment.end, rangeStart, rangeEnd, chartAreaWidth, effectiveLeftMargin);
            const segmentWidth = Math.max(0, segmentEndX - x);
            return (
              <g key={segment.start}>
                <rect
                  x={x}
                  y={0}
                  width={segmentWidth}
                  height={MAJOR_AXIS_HEIGHT}
                  fill={index % 2 === 0 ? "rgba(255, 255, 255, 0.56)" : "rgba(244, 239, 231, 0.58)"}
                />
                {segmentWidth >= 88 ? (
                  <text
                    x={x + 8}
                    y={16}
                    textAnchor="start"
                    fill="var(--muted)"
                    fontSize="0.72rem"
                    fontWeight={700}
                    style={{ letterSpacing: "0.02em" }}
                  >
                    {segment.label}
                  </text>
                ) : null}
              </g>
            );
          })}
          <line
            x1={effectiveLeftMargin}
            y1={MAJOR_AXIS_HEIGHT}
            x2={containerWidth - RIGHT_MARGIN}
            y2={MAJOR_AXIS_HEIGHT}
            stroke="rgba(92, 70, 43, 0.14)"
            strokeWidth={1}
          />
          {daySegments.slice(1).map((segment) => {
            const x = timeToX(segment.start, rangeStart, rangeEnd, chartAreaWidth, effectiveLeftMargin);
            return (
              <line
                key={segment.start}
                x1={x}
                y1={0}
                x2={x}
                y2={TOP_MARGIN + actorRows.length * ROW_HEIGHT}
                stroke="rgba(92, 70, 43, 0.18)"
                strokeWidth={1.2}
              />
            );
          })}
        </g>

        {/* Grid lines (stronger at hour boundaries) */}
        <g>
          {ticks.map((tick) => {
            const x = timeToX(tick, rangeStart, rangeEnd, chartAreaWidth, effectiveLeftMargin);
            const strong = isHourBoundary(tick);
            return (
              <line
                key={tick}
                x1={x}
                y1={TOP_MARGIN}
                x2={x}
                y2={TOP_MARGIN + actorRows.length * ROW_HEIGHT}
                stroke={strong ? "rgba(92, 70, 43, 0.16)" : "rgba(92, 70, 43, 0.08)"}
                strokeWidth={strong ? 1 : 0.5}
              />
            );
          })}
        </g>

        {/* Horizontal row separators */}
        <g>
          {actorRows.map((_actor, index) =>
            index > 0 ? (
              <line
                key={_actor.id}
                x1={effectiveLeftMargin}
                y1={TOP_MARGIN + index * ROW_HEIGHT}
                x2={containerWidth - RIGHT_MARGIN}
                y2={TOP_MARGIN + index * ROW_HEIGHT}
                stroke="rgba(92, 70, 43, 0.08)"
                strokeWidth={0.5}
              />
            ) : null,
          )}
        </g>

        {/* Alternating row backgrounds */}
        <g>
          {actorRows.map((actor, index) => (
            <rect
              key={actor.id}
              x={effectiveLeftMargin}
              y={TOP_MARGIN + index * ROW_HEIGHT}
              width={chartAreaWidth}
              height={ROW_HEIGHT}
              fill={index % 2 === 0 ? "rgba(255, 255, 255, 0.3)" : "transparent"}
            />
          ))}
        </g>

        {/* Time axis labels */}
        <g>
          {ticks.map((tick, index) => {
            const x = timeToX(tick, rangeStart, rangeEnd, chartAreaWidth, effectiveLeftMargin);
            const label = formatTickLabel(tick, index, rangeDuration);
            const isBold = isMidnight(tick);
            return (
              <text
                key={tick}
                x={x}
                y={TOP_MARGIN - 18}
                textAnchor="middle"
                fill={isBold ? "var(--ink)" : "var(--muted)"}
                fontSize="0.72rem"
                fontWeight={isBold ? 700 : 600}
              >
                {label}
              </text>
            );
          })}
        </g>

        {/* Actor labels */}
        <g>
          {actorRows.map((actor, index) => (
            <text
              key={actor.id}
              x={effectiveLeftMargin - 10}
              y={TOP_MARGIN + index * ROW_HEIGHT + ROW_HEIGHT / 2}
              textAnchor="end"
              dominantBaseline="central"
              fill="var(--ink)"
              fontSize="0.82rem"
              fontWeight={600}
            >
              {actor.id}
            </text>
          ))}
        </g>

        {/* Assignment bars */}
        <g>
          <defs>
            {assignments.map((assignment) => {
              const rowIndex = actorIndexMap.get(assignment.actorId);
              if (rowIndex === undefined) return null;
              const x1 = timeToX(assignment.startTimeMs, rangeStart, rangeEnd, chartAreaWidth, effectiveLeftMargin);
              const x2 = timeToX(assignment.endTimeMs, rangeStart, rangeEnd, chartAreaWidth, effectiveLeftMargin);
              const barWidth = Math.max(x2 - x1, MIN_BAR_WIDTH_PX);
              const barY = TOP_MARGIN + rowIndex * ROW_HEIGHT + BAR_PADDING_Y;
              const barHeight = ROW_HEIGHT - 2 * BAR_PADDING_Y;
              const clipId = `gantt-clip-${assignmentDomKey(assignment)}`;
              return (
                <clipPath key={clipId} id={clipId}>
                  <rect x={x1 + 6} y={barY} width={Math.max(0, barWidth - 12)} height={barHeight} />
                </clipPath>
              );
            })}
          </defs>
          {assignments.map((assignment) => {
            const rowIndex = actorIndexMap.get(assignment.actorId);
            if (rowIndex === undefined) return null;

            const x1 = timeToX(assignment.startTimeMs, rangeStart, rangeEnd, chartAreaWidth, effectiveLeftMargin);
            const x2 = timeToX(assignment.endTimeMs, rangeStart, rangeEnd, chartAreaWidth, effectiveLeftMargin);
            const barWidth = Math.max(x2 - x1, MIN_BAR_WIDTH_PX);
            const barY = TOP_MARGIN + rowIndex * ROW_HEIGHT + BAR_PADDING_Y;
            const barHeight = ROW_HEIGHT - 2 * BAR_PADDING_Y;
            const domKey = assignmentDomKey(assignment);
            const clipId = `gantt-clip-${domKey}`;

            return (
              <g
                key={domKey}
                onMouseEnter={(event) => {
                  const task = taskMap.get(assignment.taskId);
                  setTooltip((prev) => {
                    if (prev?.pinned === true) return prev;
                    return createTooltipState(assignment, task, event.clientX, event.clientY, false);
                  });
                }}
                onFocus={() => {
                  const task = taskMap.get(assignment.taskId);
                  setTooltip((prev) => {
                    if (prev?.pinned === true) return prev;
                    return createBarTooltipState(assignment, task, x1, barY, barWidth, barHeight, false);
                  });
                }}
                onMouseLeave={() => {
                  setTooltip((prev) => (prev?.pinned === true ? prev : undefined));
                }}
                onBlur={() => {
                  setTooltip((prev) => (prev?.pinned === true ? prev : undefined));
                }}
                onClick={(event) => {
                  const task = taskMap.get(assignment.taskId);
                  event.stopPropagation();
                  setTooltip((prev) => {
                    if (prev?.pinned === true && isSameAssignment(prev.assignment, assignment)) {
                      return undefined;
                    }
                    return createTooltipState(assignment, task, event.clientX, event.clientY, true);
                  });
                }}
                onKeyDown={(event) => {
                  if (event.key === "Enter" || event.key === " ") {
                    const task = taskMap.get(assignment.taskId);
                    event.preventDefault();
                    event.stopPropagation();
                    setTooltip((prev) => {
                      if (prev?.pinned === true && isSameAssignment(prev.assignment, assignment)) {
                        return undefined;
                      }
                      return createBarTooltipState(assignment, task, x1, barY, barWidth, barHeight, true);
                    });
                  }

                  if (event.key === "Escape") {
                    setTooltip(undefined);
                  }
                }}
                role="button"
                tabIndex={0}
                aria-label={`${assignment.taskId} assigned to ${assignment.actorId} from ${assignment.startAt} to ${assignment.endAt}`}
                style={{ cursor: "pointer" }}
              >
                <rect
                  x={x1}
                  y={barY}
                  width={barWidth}
                  height={barHeight}
                  rx={BAR_RADIUS}
                  fill={palette.fill}
                  stroke={palette.stroke}
                  strokeWidth={1}
                />
                <text
                  x={x1 + 8}
                  y={barY + barHeight / 2}
                  dominantBaseline="central"
                  clipPath={`url(#${clipId})`}
                  fill={palette.text}
                  fontSize="0.78rem"
                  fontWeight={700}
                  style={{ pointerEvents: "none" }}
                >
                  {assignment.taskId}
                  {barWidth > 120 ? (
                    <tspan dx={6} fontWeight={500} opacity={0.7} fontSize="0.7rem">
                      {formatDurationShort(assignment.endTimeMs - assignment.startTimeMs)}
                    </tspan>
                  ) : null}
                </text>
              </g>
            );
          })}
        </g>

        {/* Deadline markers — small triangles at the top of each actor row */}
        <g>
          {assignments.map((assignment) => {
            const task = taskMap.get(assignment.taskId);
            if (task === undefined || task.deadlineMs <= 0) return null;
            if (task.deadlineMs < rangeStart || task.deadlineMs > rangeEnd) return null;
            const rowIndex = actorIndexMap.get(assignment.actorId);
            if (rowIndex === undefined) return null;

            const dx = timeToX(task.deadlineMs, rangeStart, rangeEnd, chartAreaWidth, effectiveLeftMargin);
            const rowTop = TOP_MARGIN + rowIndex * ROW_HEIGHT;
            return (
              <g key={`dl-${assignment.taskId}-${assignment.actorId}`}>
                <line
                  x1={dx}
                  y1={rowTop + BAR_PADDING_Y}
                  x2={dx}
                  y2={rowTop + ROW_HEIGHT - BAR_PADDING_Y}
                  stroke="var(--danger)"
                  strokeWidth={1}
                  strokeDasharray="3 3"
                  opacity={0.5}
                />
                <polygon
                  points={`${String(dx - 3.5)},${String(rowTop + BAR_PADDING_Y)} ${String(dx + 3.5)},${String(rowTop + BAR_PADDING_Y)} ${String(dx)},${String(rowTop + BAR_PADDING_Y + 5)}`}
                  fill="var(--danger)"
                  opacity={0.6}
                />
              </g>
            );
          })}
        </g>

        {/* Now indicator */}
        {nowVisible ? (
          <g>
            <line
              x1={nowX}
              y1={TOP_MARGIN}
              x2={nowX}
              y2={TOP_MARGIN + actorRows.length * ROW_HEIGHT}
              stroke="var(--accent)"
              strokeWidth={1.5}
              strokeDasharray="6 4"
              opacity={0.7}
            />
            <text
              x={nowX}
              y={TOP_MARGIN - 6}
              textAnchor="middle"
              fill="var(--accent)"
              fontSize="0.68rem"
              fontWeight={700}
              style={{ textTransform: "uppercase", letterSpacing: "0.1em" }}
            >
              now
            </text>
          </g>
        ) : null}
      </svg>

      {/* Tooltip rendered via portal so it escapes .panel's backdrop-filter containing block */}
      {tooltip !== undefined ? createPortal(
        <div
          className={`gantt-tooltip${tooltip.pinned ? " gantt-tooltip--pinned" : ""}`}
          style={{
            position: "fixed",
            left: tooltip.x,
            top: tooltip.y,
          }}
          onClick={(event) => { event.stopPropagation(); }}
        >
          <strong>{tooltip.assignment.taskId}</strong>
          <p>Actor: {tooltip.assignment.actorId}</p>
          <p>
            {formatTooltipTime(tooltip.assignment.startTimeMs)} &rarr; {formatTooltipTime(tooltip.assignment.endTimeMs)}
          </p>
          <p className="muted" style={{ fontSize: "0.78rem" }}>
            Duration: {formatDurationShort(tooltip.assignment.endTimeMs - tooltip.assignment.startTimeMs)}
          </p>
          {tooltip.task !== undefined ? (
            <>
              <p>Priority: {String(tooltip.task.priority)}</p>
              <p>Duration: {String(tooltip.task.durationMinutes)} min</p>
              {tooltip.task.deadlineAt ? <p>Deadline: {tooltip.task.deadlineAt}</p> : null}
              {tooltip.task.dependencyTaskIds.length > 0 ? (
                <p>Depends on: {tooltip.task.dependencyTaskIds.join(", ")}</p>
              ) : null}
            </>
          ) : null}
          <button
            className="gantt-tooltip-dismiss"
            onClick={() => { setTooltip(undefined); }}
            aria-label="Dismiss"
          >
            &times;
          </button>
        </div>,
        document.body,
      ) : null}
    </div>
  );
}

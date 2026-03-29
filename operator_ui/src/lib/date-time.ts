const MINUTE_IN_MS = 60_000;

export function parseNumber(value: unknown): number {
  if (typeof value === "number") {
    return Number.isFinite(value) ? value : 0;
  }

  if (typeof value === "string" && value.length > 0) {
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : 0;
  }

  return 0;
}

export function formatDateTime(value: unknown): string {
  const timestamp = parseNumber(value);
  if (timestamp <= 0) {
    return "not scheduled";
  }

  return new Intl.DateTimeFormat("en-GB", {
    day: "2-digit",
    month: "short",
    hour: "2-digit",
    minute: "2-digit",
  }).format(new Date(timestamp));
}

export function formatClockTime(value: unknown): string {
  const timestamp = parseNumber(value);
  if (timestamp <= 0) {
    return "not scheduled";
  }

  return new Intl.DateTimeFormat("en-GB", {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  }).format(new Date(timestamp));
}

export function toDatetimeLocalValue(value: unknown): string {
  const timestamp = parseNumber(value);
  if (timestamp <= 0) {
    return "";
  }

  const date = new Date(timestamp);
  const localDate = new Date(date.getTime() - date.getTimezoneOffset() * MINUTE_IN_MS);
  return localDate.toISOString().slice(0, 16);
}

export function fromDatetimeLocalValue(value: string): number {
  if (value.length === 0) {
    return 0;
  }

  return new Date(value).getTime();
}

export function minutesToMilliseconds(minutes: number): number {
  return Math.max(0, Math.round(minutes * MINUTE_IN_MS));
}

export function millisecondsToMinutes(milliseconds: unknown): number {
  return Math.max(0, Math.round(parseNumber(milliseconds) / MINUTE_IN_MS));
}

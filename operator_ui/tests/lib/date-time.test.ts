import { describe, expect, it } from "vitest";
import {
  formatClockTime,
  formatDateTime,
  fromDatetimeLocalValue,
  millisecondsToMinutes,
  minutesToMilliseconds,
  parseNumber,
  toDatetimeLocalValue,
} from "../../src/lib/date-time";

describe("date-time helpers", () => {
  it("parses numbers and formats timestamps", () => {
    const timestamp = Date.parse("2026-03-29T10:42:00Z");

    expect(parseNumber(String(timestamp))).toBe(timestamp);
    expect(formatDateTime(timestamp)).toContain("29 Mar");
    expect(formatClockTime(timestamp)).toMatch(/^\d{2}:\d{2}:\d{2}$/);
  });

  it("round-trips datetime-local values and duration helpers", () => {
    const localValue = "2026-03-29T11:15";
    const unixTime = fromDatetimeLocalValue(localValue);

    expect(toDatetimeLocalValue(unixTime)).toBe(localValue);
    expect(minutesToMilliseconds(15)).toBe(900000);
    expect(millisecondsToMinutes(900000)).toBe(15);
  });

  it("handles invalid and empty values safely", () => {
    expect(parseNumber("not-a-number")).toBe(0);
    expect(formatDateTime(0)).toBe("not scheduled");
    expect(formatClockTime(undefined)).toBe("not scheduled");
    expect(toDatetimeLocalValue(null)).toBe("");
    expect(fromDatetimeLocalValue("")).toBe(0);
    expect(minutesToMilliseconds(-5)).toBe(0);
    expect(millisecondsToMinutes("invalid")).toBe(0);
  });
});

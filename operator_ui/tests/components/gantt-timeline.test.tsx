import { fireEvent, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { GanttTimeline } from "../../src/components/gantt-timeline";
import { detailWithChanges } from "../fixtures";

describe("GanttTimeline", () => {
  const originalClientWidth = Object.getOwnPropertyDescriptor(HTMLElement.prototype, "clientWidth");
  let dateNowSpy: { mockRestore: () => void } | undefined;
  const setClientWidth = (width: number) => {
    Object.defineProperty(HTMLElement.prototype, "clientWidth", {
      configurable: true,
      get: () => width,
    });
  };

  beforeEach(() => {
    dateNowSpy = vi.spyOn(Date, "now").mockReturnValue(Date.parse("2026-03-29T10:42:00Z"));
    setClientWidth(960);
  });

  afterEach(() => {
    dateNowSpy?.mockRestore();
    if (originalClientWidth !== undefined) {
      Object.defineProperty(HTMLElement.prototype, "clientWidth", originalClientWidth);
      return;
    }

    Reflect.deleteProperty(HTMLElement.prototype, "clientWidth");
  });

  it("shows a date-aware timeline header with compact navigation controls", async () => {
    render(
      <GanttTimeline
        assignments={detailWithChanges.assignments}
        actors={detailWithChanges.actors}
        tasks={detailWithChanges.tasks}
        workflowState={detailWithChanges.summary.state}
      />,
    );

    expect(await screen.findByText(/^Sun 29 Mar 00:00 to Mon 30 Mar 00:00$/)).toBeInTheDocument();
    expect(screen.getByText(/^Today · 3 actors$/)).toBeInTheDocument();
    expect(screen.getByRole("group", { name: "Timeline navigation" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Past" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Reset" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "Future" })).toBeInTheDocument();
    expect(screen.getByText(/scroll to zoom/i)).toBeInTheDocument();
  });

  it("zooms the visible horizon on mouse wheel and reset returns to today", async () => {
    render(
      <GanttTimeline
        assignments={detailWithChanges.assignments}
        actors={detailWithChanges.actors}
        tasks={detailWithChanges.tasks}
        workflowState={detailWithChanges.summary.state}
      />,
    );

    expect(await screen.findByText(/^Sun 29 Mar 00:00 to Mon 30 Mar 00:00$/)).toBeInTheDocument();

    fireEvent.wheel(screen.getByRole("img", { name: /schedule timeline/i }), {
      deltaY: -120,
      clientX: 640,
    });

    expect(screen.queryByText(/^Sun 29 Mar 00:00 to Mon 30 Mar 00:00$/)).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Reset" })).toBeEnabled();
    expect(screen.getByText(/window/i)).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Reset" }));

    expect(await screen.findByText(/^Sun 29 Mar 00:00 to Mon 30 Mar 00:00$/)).toBeInTheDocument();
    expect(screen.getByText(/^Today · 3 actors$/)).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Reset" })).toBeDisabled();
  });

  it("ignores no-op wheel zooms and hides the now indicator when panned away", async () => {
    render(
      <GanttTimeline
        assignments={detailWithChanges.assignments}
        actors={detailWithChanges.actors}
        tasks={detailWithChanges.tasks}
        workflowState={detailWithChanges.summary.state}
      />,
    );

    const timeline = await screen.findByRole("img", { name: /schedule timeline/i });
    expect(screen.getByText("now")).toBeInTheDocument();

    fireEvent.wheel(timeline, {
      deltaY: 0,
      clientX: 640,
    });
    expect(screen.getByText(/^Today · 3 actors$/)).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Future" }));
    fireEvent.click(screen.getByRole("button", { name: "Future" }));

    expect(screen.queryByText("now")).not.toBeInTheDocument();
  });

  it("opens assignment details from keyboard interaction", async () => {
    render(
      <GanttTimeline
        assignments={detailWithChanges.assignments}
        actors={detailWithChanges.actors}
        tasks={detailWithChanges.tasks}
        workflowState={detailWithChanges.summary.state}
      />,
    );

    const assignment = await screen.findByRole("button", {
      name: /pick-108 assigned to robot_4/i,
    });

    fireEvent.keyDown(assignment, { key: "Enter" });

    expect(await screen.findByText("Actor: robot_4")).toBeInTheDocument();
    expect(screen.getByText("Priority: 8")).toBeInTheDocument();
  });

  it("renders an explicit empty state when there are no assignments", () => {
    render(
      <GanttTimeline
        assignments={[]}
        actors={detailWithChanges.actors}
        tasks={detailWithChanges.tasks}
        workflowState={detailWithChanges.summary.state}
      />,
    );

    expect(screen.getByText("No assignments are scheduled for this plan.")).toBeInTheDocument();
  });

  it("renders a compact fallback when the timeline container is too narrow", () => {
    setClientWidth(250);

    render(
      <GanttTimeline
        assignments={detailWithChanges.assignments}
        actors={detailWithChanges.actors}
        tasks={detailWithChanges.tasks}
        workflowState={detailWithChanges.summary.state}
      />,
    );

    expect(screen.getByText("Widen this panel to view the schedule timeline.")).toBeInTheDocument();
  });

  it("supports hover and click tooltip interactions for longer multi-day assignments", async () => {
    const assignment = {
      taskId: "route-42",
      actorId: "robot_9",
      startTimeMs: Date.parse("2026-04-01T08:00:00Z"),
      endTimeMs: Date.parse("2026-04-03T20:00:00Z"),
      startAt: "01 Apr, 08:00",
      endAt: "03 Apr, 20:00",
    };
    const task = {
      ...detailWithChanges.tasks[0],
      id: "route-42",
      durationMs: assignment.endTimeMs - assignment.startTimeMs,
      durationMinutes: Math.round((assignment.endTimeMs - assignment.startTimeMs) / 60_000),
      deadlineMs: Date.parse("2026-04-02T12:00:00Z"),
      deadlineAt: "02 Apr, 12:00",
      dependencyTaskIds: ["prep-1"],
    };

    render(
      <GanttTimeline
        assignments={[assignment]}
        actors={[{ id: "robot_9", type: "robot", capabilities: ["pick"] }]}
        tasks={[task]}
        workflowState="failed"
      />,
    );

    expect(await screen.findByText(/^Sun 29 Mar 00:00 to Mon 30 Mar 00:00$/)).toBeInTheDocument();
    expect(screen.getByText(/^Today · 1 actor$/)).toBeInTheDocument();

    const bar = screen.getByRole("button", {
      name: /route-42 assigned to robot_9/i,
    });

    fireEvent.mouseEnter(bar, { clientX: 220, clientY: 180 });
    expect(await screen.findByText("Actor: robot_9")).toBeInTheDocument();
    expect(screen.getByText(/Depends on: prep-1/)).toBeInTheDocument();

    fireEvent.mouseLeave(bar);
    expect(screen.queryByText("Actor: robot_9")).not.toBeInTheDocument();

    fireEvent.click(bar, { clientX: 240, clientY: 200 });
    expect(await screen.findByText("Duration: 60h")).toBeInTheDocument();
    expect(screen.getByText("Priority: 8")).toBeInTheDocument();

    fireEvent.keyDown(bar, { key: "Escape" });
    expect(screen.queryByText("Actor: robot_9")).not.toBeInTheDocument();
  });

  it("shows taskless tooltips without task metadata and toggles pinned state from the keyboard", async () => {
    const assignment = {
      taskId: "orphan-task",
      actorId: "robot_4",
      startTimeMs: Date.parse("2026-03-29T09:00:00Z"),
      endTimeMs: Date.parse("2026-03-29T09:20:00Z"),
      startAt: "29 Mar, 09:00",
      endAt: "29 Mar, 09:20",
    };

    render(
      <GanttTimeline
        assignments={[assignment]}
        actors={detailWithChanges.actors}
        tasks={detailWithChanges.tasks}
        workflowState="planning"
      />,
    );

    const bar = await screen.findByRole("button", {
      name: /orphan-task assigned to robot_4/i,
    });

    fireEvent.mouseEnter(bar, { clientX: 220, clientY: 180 });
    expect(await screen.findByText("Actor: robot_4")).toBeInTheDocument();
    expect(screen.queryByText(/^Priority:/)).not.toBeInTheDocument();
    expect(screen.queryByText(/^Deadline:/)).not.toBeInTheDocument();

    fireEvent.mouseLeave(bar);
    expect(screen.queryByText("Actor: robot_4")).not.toBeInTheDocument();

    fireEvent.keyDown(bar, { key: " " });
    expect(await screen.findByText("Actor: robot_4")).toBeInTheDocument();

    fireEvent.keyDown(bar, { key: " " });
    expect(screen.queryByText("Actor: robot_4")).not.toBeInTheDocument();
  });

  it("supports focus previews and dismisses pinned details from the timeline background", async () => {
    render(
      <GanttTimeline
        assignments={detailWithChanges.assignments}
        actors={detailWithChanges.actors}
        tasks={detailWithChanges.tasks}
        workflowState={detailWithChanges.summary.state}
      />,
    );

    const bar = await screen.findByRole("button", {
      name: /pick-108 assigned to robot_4/i,
    });

    fireEvent.focus(bar);
    expect(await screen.findByText("Actor: robot_4")).toBeInTheDocument();

    fireEvent.blur(bar);
    expect(screen.queryByText("Actor: robot_4")).not.toBeInTheDocument();

    fireEvent.click(bar, { clientX: 200, clientY: 160 });
    expect(await screen.findByText("Actor: robot_4")).toBeInTheDocument();

    fireEvent.click(screen.getByRole("img", { name: /schedule timeline/i }));
    expect(screen.queryByText("Actor: robot_4")).not.toBeInTheDocument();

    fireEvent.click(bar, { clientX: 200, clientY: 160 });
    expect(await screen.findByText("Actor: robot_4")).toBeInTheDocument();

    fireEvent.click(screen.getByRole("button", { name: "Dismiss" }));
    expect(screen.queryByText("Actor: robot_4")).not.toBeInTheDocument();
  });
});

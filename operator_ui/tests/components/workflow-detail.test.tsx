import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import { AuditTrail, EventTimeline, WorkflowDetail } from "../../src/components/workflow-detail";
import { detailWithChanges, detailWithoutChanges } from "../fixtures";

describe("WorkflowDetail", () => {
  it("renders summary, assignment changes, and the event timeline", () => {
    render(<WorkflowDetail detail={detailWithChanges} />);

    expect(screen.getByRole("heading", { name: "warehouse-morning-wave" })).toBeInTheDocument();
    expect(screen.getByText("Dock 2 is congested. Freeze assignments to robot_2 until the pallet backlog clears.")).toBeInTheDocument();
    expect(screen.getByText("pick-108")).toBeInTheDocument();
    expect(screen.getByText("robot_2 @ 08:14")).toBeInTheDocument();
    expect(screen.getByText("robot_4 @ 08:19")).toBeInTheDocument();
    expect(screen.getByText("Manual intervention applied.")).toBeInTheDocument();
    expect(screen.getByText(/dispatcher:/i)).toBeInTheDocument();
  });

  it("renders an explicit empty state when no assignment diff exists", () => {
    render(<WorkflowDetail detail={detailWithoutChanges} />);

    expect(screen.getByText("No assignment diff recorded for this version transition.")).toBeInTheDocument();
    expect(screen.getByText("Investigate the failed beta plan.")).toBeInTheDocument();
  });

  it("renders an empty state when no workflow is selected", () => {
    render(<WorkflowDetail />);

    expect(screen.getByText("No workflow selected")).toBeInTheDocument();
  });

  it("renders explicit empty states for the history and audit panels", () => {
    const { rerender } = render(<EventTimeline />);

    expect(screen.getByText("Choose a workflow from the left to view its event timeline.")).toBeInTheDocument();

    rerender(<AuditTrail />);
    expect(screen.getByText("Choose a workflow from the left to view its audit trail.")).toBeInTheDocument();
  });

  it("renders empty-state messages when a selected workflow has no events or audits", () => {
    const detailWithoutHistory = {
      ...detailWithChanges,
      events: [],
      audits: [],
      operatorsNote: "No operator note recorded yet.",
    };

    const { rerender } = render(<EventTimeline detail={detailWithoutHistory} />);

    expect(screen.getByText("No events recorded yet for this workflow.")).toBeInTheDocument();

    rerender(<AuditTrail detail={detailWithoutHistory} />);
    expect(screen.getByText("No audit entries are stored yet for this workflow.")).toBeInTheDocument();
  });
});

import { render, screen } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import { WorkflowDetail } from "../../src/components/workflow-detail";
import { detailWithChanges, detailWithoutChanges } from "../fixtures";

describe("WorkflowDetail", () => {
  it("renders summary, assignment changes, and the event timeline", () => {
    render(<WorkflowDetail detail={detailWithChanges} />);

    expect(screen.getByRole("heading", { name: "warehouse-morning-wave" })).toBeInTheDocument();
    expect(screen.getByText("Dock 2 is congested. Freeze assignments to robot_2 until the pallet backlog clears.")).toBeInTheDocument();
    expect(screen.getAllByText("pick-108")).toHaveLength(2);
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
});

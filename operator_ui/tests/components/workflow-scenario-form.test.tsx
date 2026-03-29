import { fireEvent, render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import { WorkflowScenarioForm } from "../../src/components/workflow-scenario-form";
import { detailWithChanges } from "../fixtures";

describe("WorkflowScenarioForm", () => {
  it("creates a cloned workflow scenario with a shifted start time", async () => {
    const user = userEvent.setup();
    const onCreateWorkflow = vi.fn().mockResolvedValue(undefined);

    render(<WorkflowScenarioForm busy={false} detail={detailWithChanges} onCreateWorkflow={onCreateWorkflow} />);

    await user.type(screen.getByLabelText("New workflow ID"), "warehouse-late-wave");
    fireEvent.change(screen.getByLabelText("Simulation start time"), { target: { value: "2026-03-29T15:30" } });
    await user.type(screen.getByLabelText("Scenario note"), "Simulate the delayed outbound wave.");
    await user.click(screen.getByRole("button", { name: "Create workflow" }));

    expect(onCreateWorkflow).toHaveBeenCalledWith(
      expect.objectContaining({
        workflowId: "warehouse-late-wave",
        actors: detailWithChanges.actors,
        note: "Simulate the delayed outbound wave.",
      }),
    );
    const firstCall = onCreateWorkflow.mock.calls[0]?.[0] as { tasks: { requestedTimeMs: number }[] } | undefined;
    expect(firstCall?.tasks).toHaveLength(detailWithChanges.tasks.length);
    expect(firstCall?.tasks[0]?.requestedTimeMs).toBeGreaterThan(detailWithChanges.tasks[0]?.requestedTimeMs ?? 0);
  });

  it("supports creating an empty workflow shell", async () => {
    const user = userEvent.setup();
    const onCreateWorkflow = vi.fn().mockResolvedValue(undefined);

    render(<WorkflowScenarioForm busy={false} detail={detailWithChanges} onCreateWorkflow={onCreateWorkflow} />);

    await user.type(screen.getByLabelText("New workflow ID"), "empty-shell");
    await user.click(screen.getByLabelText("Clone the current task schedule into the new workflow"));
    await user.click(screen.getByRole("button", { name: "Create workflow" }));

    expect(onCreateWorkflow).toHaveBeenCalledWith(
      expect.objectContaining({
        workflowId: "empty-shell",
        tasks: [],
      }),
    );
  });

  it("shows guidance when no workflow template is selected", () => {
    render(<WorkflowScenarioForm busy={false} detail={undefined} onCreateWorkflow={vi.fn()} />);

    expect(screen.getByText(/Choose a workflow first, then create a new simulation/)).toBeInTheDocument();
  });
});

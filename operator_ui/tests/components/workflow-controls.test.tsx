import { fireEvent, render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import { WorkflowControls } from "../../src/components/workflow-controls";
import { detailWithChanges, detailWithoutChanges } from "../fixtures";

describe("WorkflowControls", () => {
  it("submits pause and intervention actions", async () => {
    const user = userEvent.setup();
    const onPause = vi.fn().mockResolvedValue(undefined);
    const onResume = vi.fn().mockResolvedValue(undefined);
    const onCancel = vi.fn().mockResolvedValue(undefined);
    const onManualIntervention = vi.fn().mockResolvedValue(undefined);

    render(
      <WorkflowControls
        busy={false}
        detail={detailWithChanges}
        onCancel={onCancel}
        onManualIntervention={onManualIntervention}
        onPause={onPause}
        onResume={onResume}
      />,
    );

    await user.click(screen.getByRole("button", { name: "Pause workflow" }));
    await user.type(screen.getByLabelText("Operator note"), "Shift robot_2 out of the dock lane.");
    await user.click(screen.getByRole("button", { name: "Record intervention" }));
    await user.click(screen.getByRole("button", { name: "Cancel workflow" }));

    expect(onPause).toHaveBeenCalled();
    expect(onManualIntervention).toHaveBeenCalledWith(
      expect.objectContaining({
        note: "Shift robot_2 out of the dock lane.",
        triggerReorchestration: true,
      }),
    );
    expect(onCancel).toHaveBeenCalled();
    expect(onResume).not.toHaveBeenCalled();
  });

  it("shows resume for paused workflows and resets the note after recording", async () => {
    const user = userEvent.setup();
    const onPause = vi.fn().mockResolvedValue(undefined);
    const onResume = vi.fn().mockResolvedValue(undefined);
    const onCancel = vi.fn().mockResolvedValue(undefined);
    const onManualIntervention = vi.fn().mockResolvedValue(undefined);
    const pausedDetail = {
      ...detailWithChanges,
      summary: {
        ...detailWithChanges.summary,
        state: "paused" as const,
      },
    };

    render(
      <WorkflowControls
        busy={false}
        detail={pausedDetail}
        onCancel={onCancel}
        onManualIntervention={onManualIntervention}
        onPause={onPause}
        onResume={onResume}
      />,
    );

    await user.click(screen.getByRole("button", { name: "Resume workflow" }));
    await user.type(screen.getByLabelText("Operator note"), "Resume after dock inspection.");
    await user.click(screen.getByLabelText("Trigger replanning after recording the note"));
    await user.click(screen.getByRole("button", { name: "Record intervention" }));

    expect(onResume).toHaveBeenCalledWith(
      expect.objectContaining({
        workflowId: pausedDetail.summary.workflowId,
        reason: "Workflow resumed by an operator.",
      }),
    );
    expect(onPause).not.toHaveBeenCalled();
    expect(onManualIntervention).toHaveBeenCalledWith(
      expect.objectContaining({
        triggerReorchestration: false,
      }),
    );
    expect(screen.getByLabelText("Operator note")).toHaveValue("");
  });

  it("disables unsafe actions for terminal or busy workflows", () => {
    render(
      <WorkflowControls
        busy
        detail={detailWithoutChanges}
        onCancel={vi.fn()}
        onManualIntervention={vi.fn()}
        onPause={vi.fn()}
        onResume={vi.fn()}
      />,
    );

    expect(screen.getByRole("button", { name: "Pause workflow" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "Cancel workflow" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "Record intervention" })).toBeDisabled();
    expect(screen.getByLabelText("Operator note")).toBeDisabled();
  });

  it("supports task and actor overrides, including remove and reset flows", async () => {
    const user = userEvent.setup();
    const onManualIntervention = vi.fn().mockResolvedValue(undefined);

    render(
      <WorkflowControls
        busy={false}
        detail={detailWithChanges}
        onCancel={vi.fn().mockResolvedValue(undefined)}
        onManualIntervention={onManualIntervention}
        onPause={vi.fn().mockResolvedValue(undefined)}
        onResume={vi.fn().mockResolvedValue(undefined)}
      />,
    );

    await user.click(screen.getByRole("button", { name: "Add task override" }));
    await user.click(screen.getByRole("button", { name: "Remove" }));
    await user.click(screen.getByRole("button", { name: "Add task override" }));
    await user.selectOptions(screen.getByLabelText("Task"), "pick-108");
    await user.type(screen.getByLabelText("New priority"), "6");
    fireEvent.change(screen.getByLabelText("New deadline"), { target: { value: "2026-03-29T13:15" } });
    await user.selectOptions(screen.getByLabelText("Pin to actor"), "robot_1");
    await user.click(screen.getByLabelText("Mark completed"));

    await user.click(screen.getByRole("button", { name: "Add actor override" }));
    const removeButtons = screen.getAllByRole("button", { name: "Remove" });
    const lastRemoveButton = removeButtons.at(-1);
    expect(lastRemoveButton).toBeDefined();
    if (lastRemoveButton === undefined) {
      throw new Error("Expected a remove button for the actor override.");
    }
    await user.click(lastRemoveButton);
    await user.click(screen.getByRole("button", { name: "Add actor override" }));
    await user.selectOptions(screen.getByLabelText("Actor"), "robot_1");
    await user.type(screen.getByLabelText("New capacity"), "4");
    await user.click(screen.getByLabelText("Mark unavailable"));

    await user.click(screen.getByRole("button", { name: "Record intervention" }));

    expect(onManualIntervention).toHaveBeenCalledWith({
      workflowId: detailWithChanges.summary.workflowId,
      note: "Manual intervention applied.",
      taskOverrides: [
        {
          taskId: "pick-108",
          completed: true,
          priority: 6,
          deadline: new Date("2026-03-29T13:15").getTime(),
          pinnedActorId: "robot_1",
        },
      ],
      actorOverrides: [
        {
          actorId: "robot_1",
          unavailable: true,
          capacity: 4,
        },
      ],
      triggerReorchestration: true,
    });

    expect(screen.getByRole("button", { name: "Record intervention" })).toBeDisabled();
    expect(screen.getByLabelText("Trigger replanning after recording the note")).toBeChecked();
  });

  it("renders nothing without a selected workflow and disables override creation when there are no options", () => {
    const { container, rerender } = render(
      <WorkflowControls
        busy={false}
        detail={undefined}
        onCancel={vi.fn().mockResolvedValue(undefined)}
        onManualIntervention={vi.fn().mockResolvedValue(undefined)}
        onPause={vi.fn().mockResolvedValue(undefined)}
        onResume={vi.fn().mockResolvedValue(undefined)}
      />,
    );

    expect(container).toBeEmptyDOMElement();

    rerender(
      <WorkflowControls
        busy={false}
        detail={{
          ...detailWithChanges,
          tasks: [],
          actors: [],
        }}
        onCancel={vi.fn().mockResolvedValue(undefined)}
        onManualIntervention={vi.fn().mockResolvedValue(undefined)}
        onPause={vi.fn().mockResolvedValue(undefined)}
        onResume={vi.fn().mockResolvedValue(undefined)}
      />,
    );

    expect(screen.getByRole("button", { name: "Add task override" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "Add actor override" })).toBeDisabled();
  });
});

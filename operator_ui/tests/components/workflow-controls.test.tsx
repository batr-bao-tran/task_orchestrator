import { render, screen } from "@testing-library/react";
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
});

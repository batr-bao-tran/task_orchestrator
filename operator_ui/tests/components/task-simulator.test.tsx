import { render, screen } from "@testing-library/react";
import { fireEvent } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import { TaskSimulator } from "../../src/components/task-simulator";
import type { WorkflowTaskMutation } from "../../src/lib/types";
import { detailWithChanges } from "../fixtures";

describe("TaskSimulator", () => {
  it("lets operators edit and delete tasks", async () => {
    const user = userEvent.setup();
    const onSaveTask = vi.fn().mockResolvedValue(undefined);
    const onDeleteTask = vi.fn().mockResolvedValue(undefined);

    render(<TaskSimulator busy={false} detail={detailWithChanges} onDeleteTask={onDeleteTask} onSaveTask={onSaveTask} />);

    await user.click(screen.getAllByRole("button", { name: "Edit" })[0]);
    await user.clear(screen.getByLabelText("Priority"));
    await user.type(screen.getByLabelText("Priority"), "9");
    await user.type(screen.getByLabelText("Change note"), "Raised priority for dock recovery.");
    await user.click(screen.getByRole("button", { name: "Update order" }));
    await user.click(screen.getAllByRole("button", { name: "Delete" })[0]);

    expect(onSaveTask).toHaveBeenCalledWith(
      expect.objectContaining({
        workflowId: detailWithChanges.summary.workflowId,
        note: "Raised priority for dock recovery.",
      }),
    );
    expect(onDeleteTask).toHaveBeenCalledWith(
      expect.objectContaining({
        workflowId: detailWithChanges.summary.workflowId,
        taskId: detailWithChanges.tasks[0]?.id,
      }),
    );
  });

  it("lets operators create a new task from the blank form", async () => {
    const user = userEvent.setup();
    const onSaveTask = vi.fn().mockResolvedValue(undefined);

    render(<TaskSimulator busy={false} detail={detailWithChanges} onDeleteTask={vi.fn()} onSaveTask={onSaveTask} />);

    await user.click(screen.getAllByRole("button", { name: "New order" })[0]);
    await user.type(screen.getByLabelText("Task ID"), "pick-250");
    fireEvent.change(screen.getByLabelText("Requested start"), { target: { value: "2026-03-29T14:10" } });
    await user.clear(screen.getByLabelText("Priority"));
    await user.type(screen.getByLabelText("Priority"), "4");
    await user.click(screen.getByRole("button", { name: "Insert order" }));

    const firstCall = onSaveTask.mock.calls[0]?.[0] as { task: { id: string } } | undefined;
    expect(firstCall).toBeDefined();
    expect(firstCall?.task.id).toBe("pick-250");
  });

  it("clears the draft and hides the simulator when no workflow is selected", async () => {
    const user = userEvent.setup();
    const onSaveTask = vi.fn().mockResolvedValue(undefined);

    const { rerender } = render(
      <TaskSimulator busy={false} detail={detailWithChanges} onDeleteTask={vi.fn()} onSaveTask={onSaveTask} />,
    );

    await user.click(screen.getAllByRole("button", { name: "Edit" })[0]);
    await user.click(screen.getByLabelText("Preemptible"));
    await user.type(screen.getByLabelText("Change note"), "Temporary change");
    await user.click(screen.getByRole("button", { name: "Clear form" }));

    expect(screen.getByLabelText("Task ID")).toHaveValue("");
    expect(screen.getByLabelText("Preemptible")).not.toBeChecked();

    rerender(<TaskSimulator busy={false} detail={undefined} onDeleteTask={vi.fn()} onSaveTask={onSaveTask} />);
    expect(screen.queryByText("Orders and task schedules")).not.toBeInTheDocument();
  });

  it("validates deadlines and captures capability and dependency changes", async () => {
    const user = userEvent.setup();
    const onSaveTask = vi.fn().mockResolvedValue(undefined);

    render(<TaskSimulator busy={false} detail={detailWithChanges} onDeleteTask={vi.fn()} onSaveTask={onSaveTask} />);

    await user.click(screen.getByRole("button", { name: "New order" }));
    await user.type(screen.getByLabelText("Task ID"), "pick-260");
    fireEvent.change(screen.getByLabelText("Requested start"), { target: { value: "2026-03-29T12:00" } });
    fireEvent.change(screen.getByLabelText("Deadline"), { target: { value: "2026-03-29T11:30" } });

    expect(screen.getByText("Deadline must be after the requested start time.")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Insert order" })).toBeDisabled();

    fireEvent.change(screen.getByLabelText("Deadline"), { target: { value: "2026-03-29T13:30" } });
    await user.selectOptions(screen.getByLabelText("Preferred actor"), "robot_1");
    await user.click(screen.getByLabelText("scan"));
    await user.click(screen.getByLabelText("pick-108"));
    await user.click(screen.getByLabelText("Mandatory"));
    await user.click(screen.getByLabelText("Preemptible"));
    await user.click(screen.getByRole("button", { name: "Insert order" }));

    const firstCall = onSaveTask.mock.calls[0]?.[0] as WorkflowTaskMutation | undefined;
    expect(firstCall).toBeDefined();
    expect(firstCall?.task.preferredActorIds).toEqual(["robot_1"]);
    expect(firstCall?.task.requiredCapabilities).toEqual(["scan"]);
    expect(firstCall?.task.dependencyTaskIds).toEqual(["pick-108"]);
    expect(firstCall?.task.mandatory).toBe(false);
    expect(firstCall?.task.preemptible).toBe(true);
  });

  it("shows an explicit empty state when a workflow has no tasks", () => {
    render(
      <TaskSimulator
        busy={false}
        detail={{
          ...detailWithChanges,
          tasks: [],
        }}
        onDeleteTask={vi.fn()}
        onSaveTask={vi.fn()}
      />,
    );

    expect(screen.getByText("No tasks are currently scheduled for this workflow.")).toBeInTheDocument();
  });
});

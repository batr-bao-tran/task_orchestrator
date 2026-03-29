import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import { WorkflowTable } from "../../src/components/workflow-table";
import { testWorkflows } from "../fixtures";

describe("WorkflowTable", () => {
  it("renders workflow rows and reports selection changes", async () => {
    const onSelect = vi.fn<(workflowId: string) => void>();
    const user = userEvent.setup();

    render(
      <WorkflowTable
        workflows={testWorkflows}
        selectedWorkflowId="warehouse-morning-wave"
        onSelect={onSelect}
      />,
    );

    expect(screen.getByText("4 tracked")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /warehouse-morning-wave/i })).toHaveClass("workflow-row--active");
    expect(screen.getByText("Expected plan version mismatch during manual intervention.")).toBeInTheDocument();

    await user.click(screen.getByRole("button", { name: /robot-recovery-demo/i }));

    expect(onSelect).toHaveBeenCalledWith("robot-recovery-demo");
  });

  it("renders an empty list state", () => {
    render(<WorkflowTable workflows={[]} selectedWorkflowId="" onSelect={vi.fn()} />);

    expect(screen.getByText("No workflows match the current filter.")).toBeInTheDocument();
  });
});

import { fireEvent, render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";
import type { useOperatorDashboard } from "../src/hooks/use-operator-dashboard";
import { detailWithChanges, testConnectors, testDashboard, testWorkflows } from "./fixtures";

type UseOperatorDashboardState = ReturnType<typeof useOperatorDashboard>;

const useOperatorDashboardMock = vi.fn<() => UseOperatorDashboardState>();
let hookState: UseOperatorDashboardState;
let setSearchQueryMock: ReturnType<typeof vi.fn>;
let refreshMock: ReturnType<typeof vi.fn>;

vi.mock("../src/hooks/use-operator-dashboard", () => ({
  useOperatorDashboard: () => useOperatorDashboardMock(),
}));

import { App } from "../src/app";

describe("App state rendering", () => {
  beforeEach(() => {
    setSearchQueryMock = vi.fn();
    refreshMock = vi.fn().mockResolvedValue(undefined);
    hookState = {
      dashboard: {
        ...testDashboard,
        connectors: testConnectors,
        selectedWorkflow: detailWithChanges,
        selectedWorkflowId: detailWithChanges.summary.workflowId,
        workflows: testWorkflows,
      },
      selectedWorkflowId: detailWithChanges.summary.workflowId,
      searchQuery: "",
      loading: false,
      mutating: false,
      errorMessage: "Control plane lag detected.",
      infoMessage: "Showing cached workflow data.",
      connectionInterrupted: false,
      modeLabel: "live",
      setSelectedWorkflowId: vi.fn(),
      setSearchQuery: setSearchQueryMock,
      refresh: refreshMock,
      upsertWorkflow: vi.fn().mockResolvedValue(undefined),
      upsertTask: vi.fn().mockResolvedValue(undefined),
      deleteTask: vi.fn().mockResolvedValue(undefined),
      pauseWorkflow: vi.fn().mockResolvedValue(undefined),
      resumeWorkflow: vi.fn().mockResolvedValue(undefined),
      cancelWorkflow: vi.fn().mockResolvedValue(undefined),
      applyManualIntervention: vi.fn().mockResolvedValue(undefined),
    };
    useOperatorDashboardMock.mockReset();
    useOperatorDashboardMock.mockReturnValue(hookState);
  });

  it("renders status banners and workspace tabs from hook state", async () => {
    const user = userEvent.setup();

    render(<App />);

    expect(screen.getByRole("alert")).toHaveTextContent("Control plane lag detected.");
    expect(screen.getByText("Showing cached workflow data.")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Refresh" })).toBeEnabled();

    fireEvent.change(screen.getByLabelText("Search workflows"), { target: { value: "robot" } });
    expect(setSearchQueryMock).toHaveBeenCalledWith("robot");

    await user.click(screen.getByRole("button", { name: "Refresh" }));
    expect(refreshMock).toHaveBeenCalled();

    await user.click(screen.getByRole("tab", { name: "Operate" }));
    expect(screen.getByRole("heading", { name: "Operator controls" })).toBeInTheDocument();

    await user.click(screen.getByRole("tab", { name: "Simulate" }));
    expect(screen.getByRole("heading", { name: "Orders and task schedules" })).toBeInTheDocument();

    await user.click(screen.getByRole("tab", { name: "History" }));
    expect(screen.getByRole("heading", { name: "Event timeline" })).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "Operator actions" })).toBeInTheDocument();
  });

  it("renders a pronounced connection-loss banner when live updates stop", () => {
    hookState = {
      ...hookState,
      connectionInterrupted: true,
      errorMessage: "We can't reach live updates right now. Please refresh and try again in a moment.",
      infoMessage: undefined,
    };
    useOperatorDashboardMock.mockReturnValue(hookState);

    render(<App />);

    expect(screen.getByRole("alert")).toHaveClass("status-banner--critical");
    expect(screen.getByText("Live updates unavailable")).toBeInTheDocument();
    expect(screen.getByText("We can't reach live updates right now. Please refresh and try again in a moment.")).toBeInTheDocument();
  });
});

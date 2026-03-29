import { createMockDashboard } from "../src/lib/mock-data";
import type { OperatorDashboard, WorkflowDetail } from "../src/lib/types";

function requireDetail(value: WorkflowDetail | undefined): WorkflowDetail {
  if (value === undefined) {
    throw new Error("Expected the mock dashboard to include a selected workflow detail.");
  }

  return value;
}

export const testDashboard: OperatorDashboard = createMockDashboard();
export const testWorkflows = testDashboard.workflows;
export const testConnectors = testDashboard.connectors;
export const detailWithChanges = requireDetail(testDashboard.selectedWorkflow);
export const detailWithoutChanges: WorkflowDetail = {
  ...detailWithChanges,
  summary: {
    ...detailWithChanges.summary,
    workflowId: "beta",
    state: "failed",
  },
  planDiff: [],
  operatorsNote: "Investigate the failed beta plan.",
};

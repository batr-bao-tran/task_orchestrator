export function buildOperatorDashboardPayload() {
  return {
    ok: true,
    serverTimeUnixMs: "1711708920000",
    stats: {
      recentEventsPersisted: "12",
      planVersionsRetained: "6",
      connectorsTracked: "2",
      workflowsTracked: "6",
      activeWorkflows: "5",
    },
    workflows: [
      {
        workflowId: "wf-submitted",
        state: "WORKFLOW_LIFECYCLE_STATE_SUBMITTED",
        updatedAtUnixMs: "1711708920000",
        latestPlanVersion: "1",
        totalEventCount: "1",
        totalAuditEntryCount: "0",
      },
      {
        workflowId: "wf-planning",
        state: "WORKFLOW_LIFECYCLE_STATE_PLANNING",
        updatedAtUnixMs: "1711708920000",
        latestPlanVersion: "2",
        totalEventCount: "2",
        totalAuditEntryCount: "1",
      },
      {
        workflowId: "wf-paused",
        state: "WORKFLOW_LIFECYCLE_STATE_PAUSED",
        updatedAtUnixMs: "1711708920000",
        latestPlanVersion: "3",
        totalEventCount: "3",
        totalAuditEntryCount: "1",
      },
      {
        workflowId: "wf-cancelled",
        state: "WORKFLOW_LIFECYCLE_STATE_CANCELLED",
        updatedAtUnixMs: "1711708920000",
        latestPlanVersion: "4",
        totalEventCount: "4",
        totalAuditEntryCount: "2",
      },
      {
        workflowId: "wf-failed",
        state: "WORKFLOW_LIFECYCLE_STATE_FAILED",
        updatedAtUnixMs: "1711708920000",
        latestPlanVersion: "5",
        totalEventCount: "5",
        totalAuditEntryCount: "2",
        lastErrorMessage: "capacity exhausted",
      },
      {
        workflowId: "wf-live",
        state: "WORKFLOW_LIFECYCLE_STATE_RECOVERING",
        updatedAtUnixMs: "1711708920000",
        latestPlanVersion: "6",
        totalEventCount: "6",
        totalAuditEntryCount: "3",
      },
    ],
    selectedWorkflowId: "wf-live",
    selectedWorkflow: {
      ok: true,
      workflow: {
        summary: {
          workflowId: "wf-live",
          state: "WORKFLOW_LIFECYCLE_STATE_PLANNED",
          updatedAtUnixMs: "1711708920000",
          latestPlanVersion: "6",
          totalEventCount: "12",
          totalAuditEntryCount: "3",
        },
        config: {
          actors: [
            { id: "robot_1", type: "robot", capacity: "2", capabilities: ["pick", "scan"] },
            { id: "robot_2", type: "robot", capabilities: ["pick"] },
          ],
          tasks: [
            {
              id: "pick-1",
              requestedTime: "1711708920000",
              duration: "900000",
              latestStartTime: "1711709220000",
              deadline: "1711710720000",
              priority: "5",
              demand: "2",
              mandatory: true,
              preemptible: false,
              allowedActorTypes: ["robot"],
              allowedActorIds: ["robot_1"],
              preferredActorIds: ["robot_1"],
              requiredCapabilities: ["pick"],
              dependencyTaskIds: ["scan-1"],
              mutuallyExclusiveTaskIds: ["pick-9"],
              actorDistances: [{ actorId: "robot_1", distance: "4" }],
              tardinessCostPerUnit: 1.5,
              earlyStartBonus: 0.5,
              phaseDurations: ["300000", "600000"],
            },
          ],
        },
        latestResponse: {
          result: {
            assignments: [
              { taskId: "pick-1", actorId: "robot_1", startTime: "1711708920000", endTime: "1711709820000" },
            ],
          },
        },
      },
      events: [
        {
          sequence: "1",
          recordedAtUnixMs: "1711708920000",
          event: {
            type: "WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED",
            detail: "accepted",
          },
        },
        {
          sequence: "2",
          recordedAtUnixMs: "1711708980000",
          event: {
            detail: "detail only",
          },
        },
      ],
      auditEntries: [
        {
          sequence: "1",
          recordedAtUnixMs: "1711708920000",
          actor: "dispatcher",
          action: "pause",
          detail: "Keep the lane clear.",
        },
      ],
    },
    selectedPlanDiff: {
      diff: {
        addedAssignments: [{ taskId: "pick-1", actorId: "robot_1", startTime: "1711708920000" }],
        removedAssignments: [{ taskId: "pick-2", actorId: "robot_2", startTime: "1711708980000" }],
        changedAssignments: [
          {
            before: { taskId: "pick-3", actorId: "robot_2", startTime: "1711709040000" },
            after: { taskId: "pick-3", actorId: "robot_1", startTime: "1711709100000" },
          },
        ],
      },
    },
    connectors: [
      { id: "webhook", kind: "Webhook source", displayName: "Warehouse", target: "/orders", enabled: true },
      { id: "erp", kind: "Queue consumer", displayName: "ERP", target: "nats://erp.orders", enabled: false },
    ],
  };
}

export function buildOperatorMutationPayload(overrides?: Record<string, unknown>) {
  return {
    ok: true,
    dashboard: buildOperatorDashboardPayload(),
    ...overrides,
  };
}

export function buildOperatorDashboardUpdatePayload(overrides?: Record<string, unknown>) {
  return {
    ok: true,
    serverTimeUnixMs: "1711708990000",
    stats: {
      recentEventsPersisted: "13",
      planVersionsRetained: "7",
      connectorsTracked: "2",
      workflowsTracked: "6",
      activeWorkflows: "5",
    },
    workflows: buildOperatorDashboardPayload().workflows,
    connectors: buildOperatorDashboardPayload().connectors,
    selectedWorkflowId: "wf-live",
    selectedWorkflow: buildOperatorDashboardPayload().selectedWorkflow,
    selectedPlanDiff: buildOperatorDashboardPayload().selectedPlanDiff,
    ...overrides,
  };
}

import { formatClockTime, formatDateTime, millisecondsToMinutes } from "./date-time";
import type {
  ConnectorBinding,
  DashboardStats,
  OperatorDashboard,
  PlanChange,
  WorkflowActor,
  WorkflowAssignment,
  WorkflowAuditEntry,
  WorkflowDetail,
  WorkflowEventRecord,
  WorkflowSummary,
  WorkflowTask,
} from "./types";

const baseTime = Date.parse("2026-03-29T10:42:00Z");

export interface MockDashboardFixtures {
  workflows: WorkflowSummary[];
  connectors: ConnectorBinding[];
  details: Record<string, WorkflowDetail>;
}

function createWorkflowSummary(
  workflowId: string,
  state: WorkflowSummary["state"],
  updatedAtUnixMs: number,
  latestPlanVersion: number,
  totalEvents: number,
  totalAuditEntries: number,
  lastError?: string,
): WorkflowSummary {
  return {
    workflowId,
    state,
    updatedAtUnixMs,
    updatedAt: formatDateTime(updatedAtUnixMs),
    latestPlanVersion,
    totalEvents,
    totalAuditEntries,
    lastError,
  };
}

function createTask(task: Partial<WorkflowTask> & Pick<WorkflowTask, "id" | "requestedTimeMs" | "durationMs">): WorkflowTask {
  const deadlineMs = task.deadlineMs ?? task.requestedTimeMs + task.durationMs + 45 * 60_000;
  const latestStartTimeMs = task.latestStartTimeMs ?? 0;

  return {
    id: task.id,
    requestedTimeMs: task.requestedTimeMs,
    requestedAt: formatDateTime(task.requestedTimeMs),
    durationMs: task.durationMs,
    durationMinutes: millisecondsToMinutes(task.durationMs),
    latestStartTimeMs,
    latestStartAt: latestStartTimeMs > 0 ? formatDateTime(latestStartTimeMs) : undefined,
    deadlineMs,
    deadlineAt: formatDateTime(deadlineMs),
    priority: task.priority ?? 1,
    demand: task.demand,
    mandatory: task.mandatory ?? true,
    preemptible: task.preemptible ?? false,
    allowedActorTypes: task.allowedActorTypes ?? [],
    allowedActorIds: task.allowedActorIds ?? [],
    preferredActorIds: task.preferredActorIds ?? [],
    requiredCapabilities: task.requiredCapabilities ?? [],
    dependencyTaskIds: task.dependencyTaskIds ?? [],
    mutuallyExclusiveTaskIds: task.mutuallyExclusiveTaskIds ?? [],
    actorDistances: task.actorDistances ?? [],
    tardinessCostPerUnit: task.tardinessCostPerUnit ?? 0,
    earlyStartBonus: task.earlyStartBonus ?? 0,
    phaseDurations: task.phaseDurations ?? [],
  };
}

function createAssignment(taskId: string, actorId: string, startTimeMs: number, endTimeMs: number): WorkflowAssignment {
  return {
    taskId,
    actorId,
    startTimeMs,
    endTimeMs,
    startAt: formatDateTime(startTimeMs),
    endAt: formatDateTime(endTimeMs),
  };
}

function createEvent(sequence: number, recordedAtUnixMs: number, type: string, detail: string): WorkflowEventRecord {
  return {
    sequence,
    recordedAtUnixMs,
    recordedAt: formatClockTime(recordedAtUnixMs),
    type,
    detail,
  };
}

function createAudit(
  sequence: number,
  recordedAtUnixMs: number,
  actor: string,
  action: string,
  detail: string,
): WorkflowAuditEntry {
  return {
    sequence,
    recordedAtUnixMs,
    recordedAt: formatClockTime(recordedAtUnixMs),
    actor,
    action,
    detail,
  };
}

function createDetail(
  summary: WorkflowSummary,
  actors: WorkflowActor[],
  tasks: WorkflowTask[],
  assignments: WorkflowAssignment[],
  planDiff: PlanChange[],
  events: WorkflowEventRecord[],
  audits: WorkflowAuditEntry[],
): WorkflowDetail {
  return {
    summary,
    actors,
    tasks,
    assignments,
    planDiff,
    events,
    audits,
    operatorsNote: audits.at(-1)?.detail ?? "No operator note recorded yet.",
  };
}

function buildDashboardStats(
  workflows: WorkflowSummary[],
  connectors: ConnectorBinding[],
  details: Record<string, WorkflowDetail>,
): DashboardStats {
  const planVersionsRetained = workflows.reduce((total, workflow) => total + workflow.latestPlanVersion, 0);
  const recentEventsPersisted = Object.values(details).reduce((total, detail) => total + detail.events.length, 0);

  return {
    recentEventsPersisted,
    planVersionsRetained,
    connectorsTracked: connectors.length,
    workflowsTracked: workflows.length,
    activeWorkflows: workflows.filter((workflow) => !["cancelled", "failed"].includes(workflow.state)).length,
  };
}

function buildMockDashboardFixtures(): MockDashboardFixtures {
  const actors: WorkflowActor[] = [
    {
      id: "robot_1",
      type: "robot",
      capacity: 2,
      capabilities: ["pick", "scan"],
    },
    {
      id: "robot_2",
      type: "robot",
      capacity: 1,
      capabilities: ["pick"],
    },
    {
      id: "robot_4",
      type: "robot",
      capacity: 2,
      capabilities: ["pick", "audit"],
    },
  ];

  const workflows = [
    createWorkflowSummary("warehouse-morning-wave", "planned", baseTime, 8, 63, 4),
    createWorkflowSummary("robot-recovery-demo", "recovering", baseTime - 2 * 60_000, 3, 18, 1),
    createWorkflowSummary("field-service-zone-7", "paused", baseTime - 11 * 60_000, 5, 44, 3),
    createWorkflowSummary(
      "shift-capacity-breach",
      "failed",
      baseTime - 14 * 60_000,
      2,
      27,
      2,
      "Expected plan version mismatch during manual intervention.",
    ),
  ];

  const details: Record<string, WorkflowDetail> = {
    "warehouse-morning-wave": createDetail(
      workflows[0],
      actors,
      [
        createTask({
          id: "pick-108",
          requestedTimeMs: baseTime - 2 * 60 * 60_000,
          durationMs: 15 * 60_000,
          priority: 8,
          preferredActorIds: ["robot_4"],
          requiredCapabilities: ["pick"],
        }),
        createTask({
          id: "pick-111",
          requestedTimeMs: baseTime - 90 * 60_000,
          durationMs: 12 * 60_000,
          priority: 7,
          preferredActorIds: ["robot_2"],
          requiredCapabilities: ["pick"],
        }),
      ],
      [
        createAssignment("pick-108", "robot_4", baseTime - 88 * 60_000, baseTime - 73 * 60_000),
        createAssignment("pick-111", "robot_2", baseTime - 70 * 60_000, baseTime - 58 * 60_000),
      ],
      [
        { taskId: "pick-108", before: "robot_2 @ 08:14", after: "robot_4 @ 08:19" },
        { taskId: "pick-111", after: "robot_2 @ 08:22" },
        { taskId: "audit-17", before: "robot_1 @ 08:26" },
      ],
      [
        createEvent(61, baseTime - 3 * 60_000, "manual intervention", "Manual intervention applied."),
        createEvent(62, baseTime - 3 * 60_000, "replanning started", "Replanning started."),
        createEvent(63, baseTime - 1 * 60_000, "run finished", "Plan version 8 stored."),
      ],
      [
        createAudit(
          4,
          baseTime - 4 * 60_000,
          "dispatcher",
          "manual_intervention",
          "Dock 2 is congested. Freeze assignments to robot_2 until the pallet backlog clears.",
        ),
      ],
    ),
    "robot-recovery-demo": createDetail(
      workflows[1],
      actors,
      [
        createTask({
          id: "scan-13",
          requestedTimeMs: baseTime - 40 * 60_000,
          durationMs: 10 * 60_000,
          priority: 5,
          preferredActorIds: ["robot_1"],
          requiredCapabilities: ["scan"],
        }),
      ],
      [createAssignment("scan-13", "robot_1", baseTime - 37 * 60_000, baseTime - 27 * 60_000)],
      [{ taskId: "scan-13", after: "robot_1 @ 09:05" }],
      [
        createEvent(16, baseTime - 2 * 60_000, "recover", "Recovery replay started."),
        createEvent(17, baseTime - 60_000, "workflow accepted", "Workflow accepted and stored."),
        createEvent(18, baseTime, "run finished", "Plan version 3 stored."),
      ],
      [createAudit(1, baseTime - 2 * 60_000, "control_plane/recovery", "recover", "Recovered from durable state.")],
    ),
    "field-service-zone-7": createDetail(
      workflows[2],
      [
        {
          id: "tech_4",
          type: "technician",
          capacity: 1,
          capabilities: ["onsite", "repair"],
        },
        {
          id: "tech_7",
          type: "technician",
          capacity: 1,
          capabilities: ["onsite", "repair"],
        },
      ],
      [
        createTask({
          id: "visit-209",
          requestedTimeMs: baseTime + 80 * 60_000,
          durationMs: 30 * 60_000,
          priority: 6,
          preferredActorIds: ["tech_7"],
          requiredCapabilities: ["repair"],
        }),
      ],
      [createAssignment("visit-209", "tech_7", baseTime + 108 * 60_000, baseTime + 138 * 60_000)],
      [{ taskId: "visit-209", before: "tech_4 @ 12:05", after: "tech_7 @ 12:30" }],
      [
        createEvent(42, baseTime - 11 * 60_000, "manual intervention", "Manual intervention applied."),
        createEvent(43, baseTime - 11 * 60_000, "pause", "Workflow paused from the control plane."),
        createEvent(44, baseTime - 10 * 60_000, "audit", "Operator note saved."),
      ],
      [
        createAudit(
          3,
          baseTime - 11 * 60_000,
          "dispatcher",
          "pause",
          "Paused by dispatcher pending technician handoff.",
        ),
      ],
    ),
    "shift-capacity-breach": createDetail(
      workflows[3],
      actors,
      [
        createTask({
          id: "pick-990",
          requestedTimeMs: baseTime - 20 * 60_000,
          durationMs: 20 * 60_000,
          priority: 10,
          preferredActorIds: ["robot_1"],
          requiredCapabilities: ["pick"],
        }),
      ],
      [],
      [],
      [
        createEvent(25, baseTime - 15 * 60_000, "replanning started", "Replanning started."),
        createEvent(26, baseTime - 14 * 60_000, "runtime override applied", "Runtime override applied."),
        createEvent(
          27,
          baseTime - 13 * 60_000,
          "request rejected",
          "Expected plan version mismatch during manual intervention.",
        ),
      ],
      [
        createAudit(
          2,
          baseTime - 13 * 60_000,
          "dispatcher",
          "manual_intervention",
          "Investigate worker pool saturation and retry with a higher overtime budget.",
        ),
      ],
    ),
  };

  const connectors: ConnectorBinding[] = [
    {
      id: "warehouse-webhook",
      kind: "Webhook source",
      displayName: "Warehouse Intake Webhook",
      target: "/connectors/warehouse/orders",
      enabled: true,
    },
    {
      id: "dispatch-cron",
      kind: "Schedule source",
      displayName: "07:30 Morning Capacity Warmup",
      target: "cron:30 7 * * *",
      enabled: true,
    },
    {
      id: "robot-callback",
      kind: "Outbound callback",
      displayName: "Robot Fleet Event Sink",
      target: "https://robots.example.internal/events",
      enabled: true,
    },
    {
      id: "erp-queue",
      kind: "Queue consumer",
      displayName: "ERP Work Order Queue",
      target: "nats://erp.work-orders",
      enabled: false,
    },
  ];

  return {
    workflows,
    connectors,
    details,
  };
}

export function createMockDashboard(): OperatorDashboard {
  return createMockDashboardFromFixtures(buildMockDashboardFixtures());
}

export function createMockDashboardFixtures(): MockDashboardFixtures {
  return buildMockDashboardFixtures();
}

function createMockDashboardFromFixtures(fixtures: MockDashboardFixtures): OperatorDashboard {
  return {
    mode: "mock",
    serverTimeUnixMs: baseTime,
    serverTime: formatDateTime(baseTime),
    stats: buildDashboardStats(fixtures.workflows, fixtures.connectors, fixtures.details),
    workflows: fixtures.workflows,
    connectors: fixtures.connectors,
    selectedWorkflowId: fixtures.workflows[0]?.workflowId ?? "",
    selectedWorkflow: fixtures.details[fixtures.workflows[0]?.workflowId ?? ""],
  };
}

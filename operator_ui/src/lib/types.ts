export type DataMode = "live" | "mock";

export type WorkflowState =
  | "submitted"
  | "planning"
  | "planned"
  | "paused"
  | "cancelled"
  | "failed"
  | "recovering";

export interface WorkflowSummary {
  workflowId: string;
  state: WorkflowState;
  updatedAtUnixMs: number;
  updatedAt: string;
  latestPlanVersion: number;
  totalEvents: number;
  totalAuditEntries: number;
  lastError?: string;
}

export interface WorkflowEventRecord {
  sequence: number;
  recordedAtUnixMs: number;
  recordedAt: string;
  detail: string;
  type: string;
}

export interface WorkflowAuditEntry {
  sequence: number;
  recordedAtUnixMs: number;
  recordedAt: string;
  actor: string;
  action: string;
  detail: string;
}

export interface PlanChange {
  taskId: string;
  before?: string;
  after?: string;
}

export interface ConnectorBinding {
  id: string;
  kind: string;
  displayName: string;
  target: string;
  enabled: boolean;
}

export interface DashboardStats {
  recentEventsPersisted: number;
  planVersionsRetained: number;
  connectorsTracked: number;
  workflowsTracked: number;
  activeWorkflows: number;
}

export interface WorkflowActor {
  id: string;
  type: string;
  capacity?: number;
  capabilities: string[];
}

export interface WorkflowTaskDistance {
  actorId: string;
  distance: number;
}

export interface WorkflowTask {
  id: string;
  requestedTimeMs: number;
  requestedAt: string;
  durationMs: number;
  durationMinutes: number;
  latestStartTimeMs: number;
  latestStartAt?: string;
  deadlineMs: number;
  deadlineAt?: string;
  priority: number;
  demand?: number;
  mandatory: boolean;
  preemptible: boolean;
  allowedActorTypes: string[];
  allowedActorIds: string[];
  preferredActorIds: string[];
  requiredCapabilities: string[];
  dependencyTaskIds: string[];
  mutuallyExclusiveTaskIds: string[];
  actorDistances: WorkflowTaskDistance[];
  tardinessCostPerUnit: number;
  earlyStartBonus: number;
  phaseDurations: number[];
}

export interface WorkflowAssignment {
  taskId: string;
  actorId: string;
  startTimeMs: number;
  endTimeMs: number;
  startAt: string;
  endAt: string;
}

export interface WorkflowDetail {
  summary: WorkflowSummary;
  planDiff: PlanChange[];
  events: WorkflowEventRecord[];
  audits: WorkflowAuditEntry[];
  operatorsNote: string;
  tasks: WorkflowTask[];
  actors: WorkflowActor[];
  assignments: WorkflowAssignment[];
}

export interface OperatorDashboard {
  mode: DataMode;
  serverTimeUnixMs: number;
  serverTime: string;
  stats: DashboardStats;
  workflows: WorkflowSummary[];
  connectors: ConnectorBinding[];
  selectedWorkflowId: string;
  selectedWorkflow?: WorkflowDetail;
}

export interface TaskEditorDraft {
  id: string;
  requestedAt: string;
  deadlineAt: string;
  durationMinutes: number;
  priority: number;
  preferredActorId: string;
  mandatory: boolean;
  preemptible: boolean;
  note: string;
}

export interface WorkflowTaskMutation {
  workflowId: string;
  task: WorkflowTask;
  note?: string;
  actor?: string;
}

export interface WorkflowUpsertCommand {
  workflowId: string;
  actors: WorkflowActor[];
  tasks: WorkflowTask[];
  note?: string;
  actor?: string;
}

export interface DeleteTaskCommand {
  workflowId: string;
  taskId: string;
  note?: string;
  actor?: string;
}

export interface WorkflowActionCommand {
  workflowId: string;
  reason?: string;
  actor?: string;
}

export interface TaskStateOverride {
  taskId: string;
  completed?: boolean;
  requestedTime?: number;
  deadline?: number;
  priority?: number;
  pinnedActorId?: string;
}

export interface ActorStateOverride {
  actorId: string;
  unavailable?: boolean;
  capacity?: number;
}

export interface ManualInterventionCommand {
  workflowId: string;
  note: string;
  actor?: string;
  taskOverrides?: TaskStateOverride[];
  actorOverrides?: ActorStateOverride[];
  triggerReorchestration: boolean;
}

export interface MutationResult {
  dashboard: OperatorDashboard;
  errorMessage?: string;
}

export interface DashboardQuery {
  selectedWorkflowId?: string;
  workflowQuery?: string;
}

import { createMockDashboard, createMockDashboardFixtures } from "./mock-data";
import { formatClockTime, formatDateTime, parseNumber } from "./date-time";
import type {
  DashboardStats,
  DashboardQuery,
  DataMode,
  DeleteTaskCommand,
  ManualInterventionCommand,
  MutationResult,
  OperatorDashboard,
  PlanChange,
  WorkflowActionCommand,
  WorkflowActor,
  WorkflowAssignment,
  WorkflowAuditEntry,
  WorkflowDetail,
  WorkflowEventRecord,
  WorkflowState,
  WorkflowSummary,
  WorkflowTask,
  WorkflowUpsertCommand,
  WorkflowTaskMutation,
} from "./types";

export interface DashboardSubscription {
  close(): void;
}

export interface DashboardSubscriptionHandlers {
  onDashboard: (dashboard: OperatorDashboard) => void;
  onUpdate?: (update: DashboardStreamUpdate) => void;
  onOpen?: () => void;
  onError?: (error: Error) => void;
}

export interface DashboardStreamUpdate {
  dashboard: Pick<
    OperatorDashboard,
    "serverTimeUnixMs" | "serverTime" | "stats" | "workflows" | "connectors" | "selectedWorkflowId"
  > & { selectedWorkflow?: WorkflowDetail };
}

interface EventSourceLike {
  addEventListener(type: string, listener: EventListenerOrEventListenerObject): void;
  removeEventListener(type: string, listener: EventListenerOrEventListenerObject): void;
  close(): void;
  onopen: ((event: Event) => void) | null;
  onerror: ((event: Event) => void) | null;
}

type EventSourceFactory = (url: string) => EventSourceLike;

export interface OperatorClient {
  getDashboard(query: DashboardQuery): Promise<OperatorDashboard>;
  subscribeDashboard(query: DashboardQuery, handlers: DashboardSubscriptionHandlers): DashboardSubscription;
  upsertWorkflow(command: WorkflowUpsertCommand): Promise<MutationResult>;
  upsertTask(command: WorkflowTaskMutation): Promise<MutationResult>;
  deleteTask(command: DeleteTaskCommand): Promise<MutationResult>;
  pauseWorkflow(command: WorkflowActionCommand): Promise<MutationResult>;
  resumeWorkflow(command: WorkflowActionCommand): Promise<MutationResult>;
  cancelWorkflow(command: WorkflowActionCommand): Promise<MutationResult>;
  applyManualIntervention(command: ManualInterventionCommand): Promise<MutationResult>;
}

export interface OperatorClientConfig {
  modePreference: "auto" | "live" | "mock";
  apiBaseUrl: string;
}

function cloneDashboard<T>(value: T): T {
  if (typeof structuredClone === "function") {
    return structuredClone(value);
  }

  return JSON.parse(JSON.stringify(value)) as T;
}

function normalizeBaseUrl(baseUrl: string): string {
  return baseUrl.endsWith("/") ? baseUrl.slice(0, -1) : baseUrl;
}

function joinUrl(baseUrl: string, path: string): string {
  return `${normalizeBaseUrl(baseUrl)}${path}`;
}

function stringValue(value: unknown, fallback = ""): string {
  return typeof value === "string" ? value : fallback;
}

function stateFromProto(value: string | undefined): WorkflowState {
  switch (value) {
    case "WORKFLOW_LIFECYCLE_STATE_SUBMITTED":
      return "submitted";
    case "WORKFLOW_LIFECYCLE_STATE_PLANNING":
      return "planning";
    case "WORKFLOW_LIFECYCLE_STATE_PAUSED":
      return "paused";
    case "WORKFLOW_LIFECYCLE_STATE_CANCELLED":
      return "cancelled";
    case "WORKFLOW_LIFECYCLE_STATE_FAILED":
      return "failed";
    case "WORKFLOW_LIFECYCLE_STATE_RECOVERING":
      return "recovering";
    case "WORKFLOW_LIFECYCLE_STATE_PLANNED":
    default:
      return "planned";
  }
}

function formatEventType(value: string | undefined): string {
  if (value === undefined || value.length === 0) {
    return "event";
  }

  return value
    .replace(/^WORKFLOW_EVENT_TYPE_/, "")
    .replaceAll("_", " ")
    .toLowerCase();
}

function mapWorkflowSummary(value: Record<string, unknown>): WorkflowSummary {
  const updatedAtUnixMs = parseNumber(value.updatedAtUnixMs);

  return {
    workflowId: stringValue(value.workflowId),
    state: stateFromProto(typeof value.state === "string" ? value.state : undefined),
    updatedAtUnixMs,
    updatedAt: formatDateTime(updatedAtUnixMs),
    latestPlanVersion: parseNumber(value.latestPlanVersion),
    totalEvents: parseNumber(value.totalEventCount),
    totalAuditEntries: parseNumber(value.totalAuditEntryCount),
    lastError: typeof value.lastErrorMessage === "string" && value.lastErrorMessage.length > 0
      ? value.lastErrorMessage
      : undefined,
  };
}

function mapWorkflowActor(value: Record<string, unknown>): WorkflowActor {
  return {
    id: stringValue(value.id),
    type: stringValue(value.type),
    capacity:
      value.capacity === undefined || value.capacity === null || value.capacity === ""
        ? undefined
        : parseNumber(value.capacity),
    capabilities: Array.isArray(value.capabilities) ? value.capabilities.map(String) : [],
  };
}

function mapWorkflowTask(value: Record<string, unknown>): WorkflowTask {
  const requestedTimeMs = parseNumber(value.requestedTime);
  const durationMs = parseNumber(value.duration);
  const latestStartTimeMs = parseNumber(value.latestStartTime);
  const deadlineMs = parseNumber(value.deadline);

  return {
    id: stringValue(value.id),
    requestedTimeMs,
    requestedAt: formatDateTime(requestedTimeMs),
    durationMs,
    durationMinutes: Math.max(0, Math.round(durationMs / 60_000)),
    latestStartTimeMs,
    latestStartAt: latestStartTimeMs > 0 ? formatDateTime(latestStartTimeMs) : undefined,
    deadlineMs,
    deadlineAt: deadlineMs > 0 ? formatDateTime(deadlineMs) : undefined,
    priority: parseNumber(value.priority),
    demand:
      value.demand === undefined || value.demand === null || value.demand === "" ? undefined : parseNumber(value.demand),
    mandatory: value.mandatory === true,
    preemptible: value.preemptible === true,
    allowedActorTypes: Array.isArray(value.allowedActorTypes) ? value.allowedActorTypes.map(String) : [],
    allowedActorIds: Array.isArray(value.allowedActorIds) ? value.allowedActorIds.map(String) : [],
    preferredActorIds: Array.isArray(value.preferredActorIds) ? value.preferredActorIds.map(String) : [],
    requiredCapabilities: Array.isArray(value.requiredCapabilities) ? value.requiredCapabilities.map(String) : [],
    dependencyTaskIds: Array.isArray(value.dependencyTaskIds) ? value.dependencyTaskIds.map(String) : [],
    mutuallyExclusiveTaskIds: Array.isArray(value.mutuallyExclusiveTaskIds)
      ? value.mutuallyExclusiveTaskIds.map(String)
      : [],
    actorDistances: Array.isArray(value.actorDistances)
      ? value.actorDistances.map((distance) => ({
          actorId: stringValue((distance as Record<string, unknown>).actorId),
          distance: parseNumber((distance as Record<string, unknown>).distance),
        }))
      : [],
    tardinessCostPerUnit: Number(value.tardinessCostPerUnit ?? 0),
    earlyStartBonus: Number(value.earlyStartBonus ?? 0),
    phaseDurations: Array.isArray(value.phaseDurations) ? value.phaseDurations.map(parseNumber) : [],
  };
}

function mapAssignment(value: Record<string, unknown>): WorkflowAssignment {
  const startTimeMs = parseNumber(value.startTime);
  const endTimeMs = parseNumber(value.endTime);

  return {
    taskId: stringValue(value.taskId),
    actorId: stringValue(value.actorId),
    startTimeMs,
    endTimeMs,
    startAt: formatDateTime(startTimeMs),
    endAt: formatDateTime(endTimeMs),
  };
}

function mapEventRecord(value: Record<string, unknown>): WorkflowEventRecord {
  const recordedAtUnixMs = parseNumber(value.recordedAtUnixMs);
  const event = (value.event as Record<string, unknown> | undefined) ?? {};

  return {
    sequence: parseNumber(value.sequence),
    recordedAtUnixMs,
    recordedAt: formatClockTime(recordedAtUnixMs),
    detail: stringValue(event.detail),
    type: formatEventType(typeof event.type === "string" ? event.type : undefined),
  };
}

function mapAuditEntry(value: Record<string, unknown>): WorkflowAuditEntry {
  const recordedAtUnixMs = parseNumber(value.recordedAtUnixMs);

  return {
    sequence: parseNumber(value.sequence),
    recordedAtUnixMs,
    recordedAt: formatClockTime(recordedAtUnixMs),
    actor: stringValue(value.actor),
    action: stringValue(value.action),
    detail: stringValue(value.detail),
  };
}

function mapPlanDiff(value: Record<string, unknown> | undefined): PlanChange[] {
  if (value === undefined) {
    return [];
  }

  const addedAssignments = Array.isArray(value.addedAssignments) ? value.addedAssignments : [];
  const removedAssignments = Array.isArray(value.removedAssignments) ? value.removedAssignments : [];
  const changedAssignments = Array.isArray(value.changedAssignments) ? value.changedAssignments : [];

  return [
    ...addedAssignments.map((assignment) => {
      const record = assignment as Record<string, unknown>;
      return {
        taskId: stringValue(record.taskId),
        after: `${stringValue(record.actorId, "unassigned")} @ ${formatClockTime(record.startTime)}`,
      };
    }),
    ...removedAssignments.map((assignment) => {
      const record = assignment as Record<string, unknown>;
      return {
        taskId: stringValue(record.taskId),
        before: `${stringValue(record.actorId, "unassigned")} @ ${formatClockTime(record.startTime)}`,
      };
    }),
    ...changedAssignments.map((assignment) => {
      const record = assignment as Record<string, unknown>;
      const before = (record.before as Record<string, unknown> | undefined) ?? {};
      const after = (record.after as Record<string, unknown> | undefined) ?? {};

      return {
        taskId: stringValue(before.taskId, stringValue(after.taskId)),
        before: `${stringValue(before.actorId, "unassigned")} @ ${formatClockTime(before.startTime)}`,
        after: `${stringValue(after.actorId, "unassigned")} @ ${formatClockTime(after.startTime)}`,
      };
    }),
  ];
}

function mapWorkflowDetail(
  selectedWorkflow: Record<string, unknown> | undefined,
  selectedPlanDiff: Record<string, unknown> | undefined,
): WorkflowDetail | undefined {
  if (selectedWorkflow?.ok !== true) {
    return undefined;
  }

  const workflow = (selectedWorkflow.workflow as Record<string, unknown> | undefined) ?? {};
  const summary = mapWorkflowSummary((workflow.summary as Record<string, unknown> | undefined) ?? {});
  const config = (workflow.config as Record<string, unknown> | undefined) ?? {};
  const latestResponse = (workflow.latestResponse as Record<string, unknown> | undefined) ?? {};
  const latestResult = (latestResponse.result as Record<string, unknown> | undefined) ?? {};
  const audits = Array.isArray(selectedWorkflow.auditEntries)
    ? selectedWorkflow.auditEntries.map((entry) => mapAuditEntry(entry as Record<string, unknown>))
    : [];
  const planDiff: PlanChange[] = mapPlanDiff((selectedPlanDiff?.diff as Record<string, unknown> | undefined) ?? undefined);

  return {
    summary,
    actors: Array.isArray(config.actors) ? config.actors.map((actor) => mapWorkflowActor(actor as Record<string, unknown>)) : [],
    tasks: Array.isArray(config.tasks)
      ? config.tasks.map((task) => mapWorkflowTask(task as Record<string, unknown>)).sort((left, right) => left.requestedTimeMs - right.requestedTimeMs)
      : [],
    assignments: Array.isArray(latestResult.assignments)
      ? latestResult.assignments.map((assignment) => mapAssignment(assignment as Record<string, unknown>))
      : [],
    planDiff,
    events: Array.isArray(selectedWorkflow.events)
      ? selectedWorkflow.events.map((event) => mapEventRecord(event as Record<string, unknown>))
      : [],
    audits,
    operatorsNote: audits.at(-1)?.detail ?? "No operator note recorded yet.",
  };
}

function mapDashboardStats(value: Record<string, unknown> | undefined): DashboardStats {
  return {
    recentEventsPersisted: parseNumber(value?.recentEventsPersisted),
    planVersionsRetained: parseNumber(value?.planVersionsRetained),
    connectorsTracked: parseNumber(value?.connectorsTracked),
    workflowsTracked: parseNumber(value?.workflowsTracked),
    activeWorkflows: parseNumber(value?.activeWorkflows),
  };
}

function mapWorkflowSummaries(value: unknown): WorkflowSummary[] {
  return Array.isArray(value) ? value.map((workflow) => mapWorkflowSummary(workflow as Record<string, unknown>)) : [];
}

function mapConnectorBindings(value: unknown): OperatorDashboard["connectors"] {
  return Array.isArray(value)
    ? value.map((connector) => ({
        id: stringValue((connector as Record<string, unknown>).id),
        kind: stringValue((connector as Record<string, unknown>).kind),
        displayName: stringValue((connector as Record<string, unknown>).displayName),
        target: stringValue((connector as Record<string, unknown>).target),
        enabled: (connector as Record<string, unknown>).enabled === true,
      }))
    : [];
}

function mapDashboardResponse(payload: Record<string, unknown>, mode: DataMode): OperatorDashboard {
  const selectedWorkflowId = stringValue(payload.selectedWorkflowId);

  return {
    mode,
    serverTimeUnixMs: parseNumber(payload.serverTimeUnixMs),
    serverTime: formatDateTime(payload.serverTimeUnixMs as string | number | undefined),
    stats: mapDashboardStats((payload.stats as Record<string, unknown> | undefined) ?? undefined),
    workflows: mapWorkflowSummaries(payload.workflows),
    connectors: mapConnectorBindings(payload.connectors),
    selectedWorkflowId,
    selectedWorkflow: mapWorkflowDetail(
      payload.selectedWorkflow as Record<string, unknown> | undefined,
      payload.selectedPlanDiff as Record<string, unknown> | undefined,
    ),
  };
}

function mapDashboardStreamUpdate(payload: Record<string, unknown>): DashboardStreamUpdate {
  return {
    dashboard: {
      serverTimeUnixMs: parseNumber(payload.serverTimeUnixMs),
      serverTime: formatDateTime(payload.serverTimeUnixMs as string | number | undefined),
      stats: mapDashboardStats((payload.stats as Record<string, unknown> | undefined) ?? undefined),
      workflows: mapWorkflowSummaries(payload.workflows),
      connectors: mapConnectorBindings(payload.connectors),
      selectedWorkflowId: stringValue(payload.selectedWorkflowId),
      selectedWorkflow: mapWorkflowDetail(
        payload.selectedWorkflow as Record<string, unknown> | undefined,
        payload.selectedPlanDiff as Record<string, unknown> | undefined,
      ),
    },
  };
}

function ensureJsonResponse(payload: unknown): Record<string, unknown> {
  if (payload !== null && typeof payload === "object") {
    return payload as Record<string, unknown>;
  }

  throw new Error("Expected a JSON object from the operator API.");
}

function encodeQuery(query: DashboardQuery): string {
  const params = new URLSearchParams();
  if (query.selectedWorkflowId) {
    params.set("selectedWorkflowId", query.selectedWorkflowId);
  }
  if (query.workflowQuery) {
    params.set("workflowQuery", query.workflowQuery);
  }

  return params.toString();
}

function dashboardPath(query: DashboardQuery, stream = false): string {
  const queryString = encodeQuery(query);
  const path = stream ? "/v1/operator/dashboard:stream" : "/v1/operator/dashboard";
  return `${path}${queryString.length > 0 ? `?${queryString}` : ""}`;
}

async function requestJson(
  input: RequestInfo | URL,
  init: RequestInit | undefined,
  fetchImpl: typeof fetch,
): Promise<Record<string, unknown>> {
  const response = await fetchImpl(input, init);
  const payload = response.status === 204 ? {} : ensureJsonResponse(await response.json());
  if (!response.ok) {
    throw new Error(stringValue(payload.errorMessage, `Operator API request failed with status ${response.status.toString()}.`));
  }
  return payload;
}

function toTaskPayload(task: WorkflowTask): Record<string, unknown> {
  return {
    id: task.id,
    requestedTime: String(task.requestedTimeMs),
    duration: String(task.durationMs),
    latestStartTime: String(task.latestStartTimeMs),
    deadline: String(task.deadlineMs),
    priority: String(task.priority),
    demand: task.demand,
    mandatory: task.mandatory,
    preemptible: task.preemptible,
    allowedActorTypes: task.allowedActorTypes,
    allowedActorIds: task.allowedActorIds,
    preferredActorIds: task.preferredActorIds,
    requiredCapabilities: task.requiredCapabilities,
    dependencyTaskIds: task.dependencyTaskIds,
    mutuallyExclusiveTaskIds: task.mutuallyExclusiveTaskIds,
    actorDistances: task.actorDistances.map((distance) => ({
      actorId: distance.actorId,
      distance: String(distance.distance),
    })),
    tardinessCostPerUnit: task.tardinessCostPerUnit,
    earlyStartBonus: task.earlyStartBonus,
    phaseDurations: task.phaseDurations.map((duration) => String(duration)),
  };
}

function toActorPayload(actor: WorkflowActor): Record<string, unknown> {
  return {
    id: actor.id,
    type: actor.type,
    capacity: actor.capacity === undefined ? undefined : String(actor.capacity),
    capabilities: actor.capabilities,
  };
}

function makeMutationResult(payload: Record<string, unknown>, mode: DataMode): MutationResult {
  const dashboardPayload = ensureJsonResponse(payload.dashboard);

  return {
    dashboard: mapDashboardResponse(dashboardPayload, mode),
    errorMessage: payload.ok === true ? undefined : stringValue(payload.errorMessage, "Operator mutation failed."),
  };
}

export function readOperatorClientConfig(env: ImportMetaEnv): OperatorClientConfig {
  const rawMode = env.VITE_OPERATOR_DATA_MODE?.trim().toLowerCase();
  const modePreference = rawMode === "live" || rawMode === "mock" ? rawMode : "auto";

  return {
    modePreference,
    apiBaseUrl: env.VITE_OPERATOR_API_BASE_URL?.trim() ?? "",
  };
}

export function createLiveOperatorClient(
  apiBaseUrl: string,
  fetchImpl: typeof fetch = fetch,
  eventSourceFactory: EventSourceFactory = (url) => new EventSource(url),
): OperatorClient {
  const baseUrl = normalizeBaseUrl(apiBaseUrl);

  return {
    async getDashboard(query) {
      const payload = await requestJson(
        joinUrl(baseUrl, dashboardPath(query)),
        {
          headers: {
            Accept: "application/json",
          },
        },
        fetchImpl,
      );

      if (payload.ok !== true) {
        throw new Error(stringValue(payload.errorMessage, "Operator dashboard request failed."));
      }

      return mapDashboardResponse(payload, "live");
    },

    subscribeDashboard(query, handlers) {
      const eventSource = eventSourceFactory(joinUrl(baseUrl, dashboardPath(query, true)));
      const onDashboardMessage: EventListener = (event) => {
        const messageEvent = event as MessageEvent<string>;
        try {
          const payload = ensureJsonResponse(JSON.parse(messageEvent.data));
          handlers.onDashboard(mapDashboardResponse(payload, "live"));
        } catch (error) {
          handlers.onError?.(
            error instanceof Error ? error : new Error("Failed to parse the operator dashboard event stream."),
          );
        }
      };
      const onDashboardUpdateMessage: EventListener = (event) => {
        const messageEvent = event as MessageEvent<string>;
        try {
          const payload = ensureJsonResponse(JSON.parse(messageEvent.data));
          if (payload.ok === false) {
            throw new Error(stringValue(payload.errorMessage, "Failed to apply the operator dashboard live update."));
          }
          handlers.onUpdate?.(mapDashboardStreamUpdate(payload));
        } catch (error) {
          handlers.onError?.(
            error instanceof Error ? error : new Error("Failed to parse the operator dashboard update stream."),
          );
        }
      };

      eventSource.addEventListener("dashboard", onDashboardMessage);
      eventSource.addEventListener("dashboard-update", onDashboardUpdateMessage);
      eventSource.onopen = () => {
        handlers.onOpen?.();
      };
      eventSource.onerror = () => {
        handlers.onError?.(new Error("Live dashboard stream disconnected."));
      };

      return {
        close() {
          eventSource.removeEventListener("dashboard", onDashboardMessage);
          eventSource.removeEventListener("dashboard-update", onDashboardUpdateMessage);
          eventSource.close();
        },
      };
    },

    async upsertWorkflow(command) {
      const payload = await requestJson(
        joinUrl(baseUrl, "/v1/operator/workflows"),
        {
          method: "POST",
          headers: {
            Accept: "application/json",
            "Content-Type": "application/json",
          },
          body: JSON.stringify({
            config: {
              id: command.workflowId,
              actors: command.actors.map((actor) => toActorPayload(actor)),
              tasks: command.tasks.map((task) => toTaskPayload(task)),
            },
            note: command.note,
            actor: command.actor,
          }),
        },
        fetchImpl,
      );

      return makeMutationResult(payload, "live");
    },

    async upsertTask(command) {
      const payload = await requestJson(
        joinUrl(baseUrl, `/v1/operator/workflows/${encodeURIComponent(command.workflowId)}/tasks`),
        {
          method: "POST",
          headers: {
            Accept: "application/json",
            "Content-Type": "application/json",
          },
          body: JSON.stringify({
            workflowId: command.workflowId,
            task: toTaskPayload(command.task),
            note: command.note,
            actor: command.actor,
          }),
        },
        fetchImpl,
      );

      return makeMutationResult(payload, "live");
    },

    async deleteTask(command) {
      const payload = await requestJson(
        joinUrl(
          baseUrl,
          `/v1/operator/workflows/${encodeURIComponent(command.workflowId)}/tasks/${encodeURIComponent(command.taskId)}:delete`,
        ),
        {
          method: "POST",
          headers: {
            Accept: "application/json",
            "Content-Type": "application/json",
          },
          body: JSON.stringify(command),
        },
        fetchImpl,
      );

      return makeMutationResult(payload, "live");
    },

    async pauseWorkflow(command) {
      const payload = await requestJson(
        joinUrl(baseUrl, `/v1/operator/workflows/${encodeURIComponent(command.workflowId)}:pause`),
        {
          method: "POST",
          headers: {
            Accept: "application/json",
            "Content-Type": "application/json",
          },
          body: JSON.stringify(command),
        },
        fetchImpl,
      );

      return makeMutationResult(payload, "live");
    },

    async resumeWorkflow(command) {
      const payload = await requestJson(
        joinUrl(baseUrl, `/v1/operator/workflows/${encodeURIComponent(command.workflowId)}:resume`),
        {
          method: "POST",
          headers: {
            Accept: "application/json",
            "Content-Type": "application/json",
          },
          body: JSON.stringify(command),
        },
        fetchImpl,
      );

      return makeMutationResult(payload, "live");
    },

    async cancelWorkflow(command) {
      const payload = await requestJson(
        joinUrl(baseUrl, `/v1/operator/workflows/${encodeURIComponent(command.workflowId)}:cancel`),
        {
          method: "POST",
          headers: {
            Accept: "application/json",
            "Content-Type": "application/json",
          },
          body: JSON.stringify(command),
        },
        fetchImpl,
      );

      return makeMutationResult(payload, "live");
    },

    async applyManualIntervention(command) {
      const payload = await requestJson(
        joinUrl(baseUrl, `/v1/operator/workflows/${encodeURIComponent(command.workflowId)}:manualIntervention`),
        {
          method: "POST",
          headers: {
            Accept: "application/json",
            "Content-Type": "application/json",
          },
          body: JSON.stringify({
            workflowId: command.workflowId,
            note: command.note,
            actor: command.actor,
            triggerReorchestration: command.triggerReorchestration,
          }),
        },
        fetchImpl,
      );

      return makeMutationResult(payload, "live");
    },
  };
}

function applyMockWorkflowUpdate(
  dashboard: OperatorDashboard,
  detailStore: Partial<Record<string, WorkflowDetail>>,
  workflowId: string,
  updateDetail: (detail: WorkflowDetail) => WorkflowDetail,
): OperatorDashboard {
  const nextDashboard = cloneDashboard(dashboard);
  const storedDetail = detailStore[workflowId];
  if (storedDetail === undefined) {
    return nextDashboard;
  }

  const updatedDetail = updateDetail(cloneDashboard(storedDetail));
  detailStore[workflowId] = cloneDashboard(updatedDetail);
  nextDashboard.selectedWorkflow = updatedDetail;
  nextDashboard.selectedWorkflowId = workflowId;
  nextDashboard.serverTimeUnixMs = Date.now();
  nextDashboard.serverTime = formatDateTime(nextDashboard.serverTimeUnixMs);

  nextDashboard.workflows = nextDashboard.workflows.map((workflow) =>
    workflow.workflowId === workflowId ? updatedDetail.summary : workflow,
  );
  nextDashboard.stats.recentEventsPersisted = Math.max(nextDashboard.stats.recentEventsPersisted + 1, 1);
  nextDashboard.stats.planVersionsRetained = Math.max(nextDashboard.stats.planVersionsRetained + 1, 1);

  return nextDashboard;
}

function appendMockEvent(detail: WorkflowDetail, type: string, detailMessage: string): WorkflowDetail {
  const recordedAtUnixMs = Date.now();
  const event: WorkflowEventRecord = {
    sequence: detail.summary.totalEvents + 1,
    recordedAtUnixMs,
    recordedAt: formatClockTime(recordedAtUnixMs),
    type,
    detail: detailMessage,
  };

  return {
    ...detail,
    summary: {
      ...detail.summary,
      totalEvents: detail.summary.totalEvents + 1,
      updatedAtUnixMs: recordedAtUnixMs,
      updatedAt: formatDateTime(recordedAtUnixMs),
      latestPlanVersion: detail.summary.latestPlanVersion + 1,
    },
    events: [...detail.events, event],
  };
}

function appendMockAudit(detail: WorkflowDetail, actor: string, action: string, detailMessage: string): WorkflowDetail {
  const recordedAtUnixMs = Date.now();
  const audit: WorkflowAuditEntry = {
    sequence: detail.summary.totalAuditEntries + 1,
    recordedAtUnixMs,
    recordedAt: formatClockTime(recordedAtUnixMs),
    actor,
    action,
    detail: detailMessage,
  };

  return {
    ...detail,
    summary: {
      ...detail.summary,
      totalAuditEntries: detail.summary.totalAuditEntries + 1,
      updatedAtUnixMs: recordedAtUnixMs,
      updatedAt: formatDateTime(recordedAtUnixMs),
    },
    audits: [...detail.audits, audit],
    operatorsNote: detailMessage,
  };
}

function sortTasks(tasks: WorkflowTask[]): WorkflowTask[] {
  return [...tasks].sort((left, right) => left.requestedTimeMs - right.requestedTimeMs);
}

function refreshMockDashboardStatistics(
  dashboard: OperatorDashboard,
  detailStore: Partial<Record<string, WorkflowDetail>>,
): OperatorDashboard {
  dashboard.stats.recentEventsPersisted = Object.values(detailStore).reduce(
    (total, detail) => total + (detail?.events.length ?? 0),
    0,
  );
  dashboard.stats.planVersionsRetained = dashboard.workflows.reduce(
    (total, workflow) => total + workflow.latestPlanVersion,
    0,
  );
  dashboard.stats.connectorsTracked = dashboard.connectors.length;
  dashboard.stats.workflowsTracked = dashboard.workflows.length;
  dashboard.stats.activeWorkflows = dashboard.workflows.filter(
    (workflow) => !["cancelled", "failed"].includes(workflow.state),
  ).length;
  return dashboard;
}

function makeMockAssignments(tasks: WorkflowTask[], actors: WorkflowActor[]): WorkflowAssignment[] {
  return tasks
    .map((task) => {
      const actorId = task.preferredActorIds.at(0) ?? task.allowedActorIds.at(0) ?? actors.at(0)?.id ?? "unassigned";
      return {
        taskId: task.id,
        actorId,
        startTimeMs: task.requestedTimeMs,
        endTimeMs: task.requestedTimeMs + task.durationMs,
        startAt: formatDateTime(task.requestedTimeMs),
        endAt: formatDateTime(task.requestedTimeMs + task.durationMs),
      };
    })
    .sort((left, right) => left.startTimeMs - right.startTimeMs);
}

function buildMockWorkflowDetail(
  command: WorkflowUpsertCommand,
  existingDetail: WorkflowDetail | undefined,
): WorkflowDetail {
  const recordedAtUnixMs = Date.now();
  const tasks = sortTasks(command.tasks);
  const taskCountLabel = String(tasks.length);
  const assignments = makeMockAssignments(tasks, command.actors);
  const events: WorkflowEventRecord[] = [...(existingDetail?.events ?? [])];
  const audits: WorkflowAuditEntry[] = [...(existingDetail?.audits ?? [])];
  const acceptedSequence = events.length + 1;
  const planSequence = acceptedSequence + 1;
  const acceptedLabel = existingDetail ? "workflow updated" : "workflow accepted";
  const acceptedDetail = existingDetail
    ? `Workflow ${command.workflowId} updated from mock mode.`
    : `Workflow ${command.workflowId} accepted from mock mode.`;
  events.push({
    sequence: acceptedSequence,
    recordedAtUnixMs,
    recordedAt: formatClockTime(recordedAtUnixMs),
    type: acceptedLabel,
    detail: acceptedDetail,
  });
  events.push({
    sequence: planSequence,
    recordedAtUnixMs,
    recordedAt: formatClockTime(recordedAtUnixMs),
    type: "run finished",
    detail:
      tasks.length > 0
        ? `Planned ${taskCountLabel} task${tasks.length === 1 ? "" : "s"} for ${command.workflowId}.`
        : `Workflow ${command.workflowId} stored without tasks yet.`,
  });

  const auditDetail = command.note ?? "Workflow scenario saved from mock mode.";
  audits.push({
    sequence: audits.length + 1,
    recordedAtUnixMs,
    recordedAt: formatClockTime(recordedAtUnixMs),
    actor: command.actor ?? "operator_ui",
    action: "operator_upsert_workflow",
    detail: auditDetail,
  });

  const summary: WorkflowSummary = {
    workflowId: command.workflowId,
    state: "planned",
    updatedAtUnixMs: recordedAtUnixMs,
    updatedAt: formatDateTime(recordedAtUnixMs),
    latestPlanVersion: (existingDetail?.summary.latestPlanVersion ?? 0) + 1,
    totalEvents: events.length,
    totalAuditEntries: audits.length,
  };

  return {
    summary,
    actors: cloneDashboard(command.actors),
    tasks,
    assignments,
    planDiff: assignments.map((assignment) => ({
      taskId: assignment.taskId,
      after: `${assignment.actorId} @ ${formatClockTime(assignment.startTimeMs)}`,
    })),
    events,
    audits,
    operatorsNote: auditDetail,
  };
}

export function createMockOperatorClient(seedDashboard: OperatorDashboard = createMockDashboard()): OperatorClient {
  const fixtures = createMockDashboardFixtures();
  let dashboard = cloneDashboard(seedDashboard);
  const detailStore: Partial<Record<string, WorkflowDetail>> = cloneDashboard(fixtures.details);

  return {
    getDashboard(query) {
      const nextDashboard = cloneDashboard(dashboard);
      const selectedWorkflowId = query.selectedWorkflowId ?? nextDashboard.selectedWorkflowId;
      nextDashboard.selectedWorkflowId = selectedWorkflowId;

      if (query.workflowQuery) {
        nextDashboard.workflows = nextDashboard.workflows.filter(
          (workflow) =>
            workflow.workflowId.includes(query.workflowQuery ?? "") ||
            workflow.lastError?.includes(query.workflowQuery ?? ""),
        );
      }

      if (nextDashboard.selectedWorkflow?.summary.workflowId !== selectedWorkflowId) {
        nextDashboard.selectedWorkflow = selectedWorkflowId.length > 0 ? cloneDashboard(detailStore[selectedWorkflowId]) : undefined;
      }

      return Promise.resolve(nextDashboard);
    },

    subscribeDashboard() {
      return {
        close() {
          // Mock mode updates are returned inline from mutation responses.
        },
      };
    },

    upsertWorkflow(command) {
      const nextDashboard = cloneDashboard(dashboard);
      const nextDetail = buildMockWorkflowDetail(command, detailStore[command.workflowId]);
      detailStore[command.workflowId] = cloneDashboard(nextDetail);
      nextDashboard.selectedWorkflowId = command.workflowId;
      nextDashboard.selectedWorkflow = cloneDashboard(nextDetail);

      const existingWorkflowIndex = nextDashboard.workflows.findIndex(
        (workflow) => workflow.workflowId === command.workflowId,
      );
      if (existingWorkflowIndex >= 0) {
        nextDashboard.workflows[existingWorkflowIndex] = nextDetail.summary;
      } else {
        nextDashboard.workflows = [nextDetail.summary, ...nextDashboard.workflows];
      }

      nextDashboard.serverTimeUnixMs = Date.now();
      nextDashboard.serverTime = formatDateTime(nextDashboard.serverTimeUnixMs);
      dashboard = refreshMockDashboardStatistics(nextDashboard, detailStore);

      return Promise.resolve({ dashboard });
    },

    upsertTask(command) {
      dashboard = applyMockWorkflowUpdate(dashboard, detailStore, command.workflowId, (detail) => {
        const existingTaskIndex = detail.tasks.findIndex((task) => task.id === command.task.id);
        const nextTasks = [...detail.tasks];
        if (existingTaskIndex >= 0) {
          nextTasks[existingTaskIndex] = command.task;
        } else {
          nextTasks.push(command.task);
        }

        const assignmentActor = command.task.preferredActorIds.at(0) ?? detail.actors.at(0)?.id ?? "unassigned";
        const nextAssignments = detail.assignments.filter((assignment) => assignment.taskId !== command.task.id);
        nextAssignments.push(
          {
            taskId: command.task.id,
            actorId: assignmentActor,
            startTimeMs: command.task.requestedTimeMs,
            endTimeMs: command.task.requestedTimeMs + command.task.durationMs,
            startAt: formatDateTime(command.task.requestedTimeMs),
            endAt: formatDateTime(command.task.requestedTimeMs + command.task.durationMs),
          },
        );

        let updatedDetail = appendMockEvent(
          detail,
          "run finished",
          existingTaskIndex >= 0 ? `Task ${command.task.id} rescheduled from the mock console.` : `Task ${command.task.id} inserted in the mock console.`,
        );
        updatedDetail = appendMockAudit(
          updatedDetail,
          command.actor ?? "operator_ui",
          existingTaskIndex >= 0 ? "operator_update_task" : "operator_insert_task",
          command.note ?? `Task ${command.task.id} saved from mock mode.`,
        );

        const nextDetail = {
          ...updatedDetail,
          tasks: sortTasks(nextTasks),
          assignments: nextAssignments.sort((left, right) => left.startTimeMs - right.startTimeMs),
          planDiff: [
            {
              taskId: command.task.id,
              after: `${assignmentActor} @ ${formatClockTime(command.task.requestedTimeMs)}`,
            },
          ],
        };
        return nextDetail;
      });
      dashboard = refreshMockDashboardStatistics(dashboard, detailStore);

      return Promise.resolve({ dashboard });
    },

    deleteTask(command) {
      dashboard = applyMockWorkflowUpdate(dashboard, detailStore, command.workflowId, (detail) => {
        const deletedTask = detail.tasks.find((task) => task.id === command.taskId);
        let updatedDetail = appendMockEvent(detail, "run finished", `Task ${command.taskId} removed from the mock console.`);
        updatedDetail = appendMockAudit(
          updatedDetail,
          command.actor ?? "operator_ui",
          "operator_delete_task",
          command.note ?? `Task ${command.taskId} deleted from mock mode.`,
        );

        const nextDetail = {
          ...updatedDetail,
          tasks: detail.tasks.filter((task) => task.id !== command.taskId),
          assignments: detail.assignments.filter((assignment) => assignment.taskId !== command.taskId),
          planDiff: deletedTask
            ? [
                {
                  taskId: command.taskId,
                  before: `${deletedTask.preferredActorIds.at(0) ?? "unassigned"} @ ${formatClockTime(deletedTask.requestedTimeMs)}`,
                },
              ]
            : [],
        };
        return nextDetail;
      });
      dashboard = refreshMockDashboardStatistics(dashboard, detailStore);

      return Promise.resolve({ dashboard });
    },

    pauseWorkflow(command) {
      dashboard = applyMockWorkflowUpdate(dashboard, detailStore, command.workflowId, (detail) => {
        const nextDetail = appendMockAudit(
          {
            ...detail,
            summary: {
              ...detail.summary,
              state: "paused",
            },
          },
          command.actor ?? "operator_ui",
          "pause",
          command.reason ?? "Workflow paused from mock mode.",
        );
        return nextDetail;
      });
      dashboard = refreshMockDashboardStatistics(dashboard, detailStore);

      return Promise.resolve({ dashboard });
    },

    resumeWorkflow(command) {
      dashboard = applyMockWorkflowUpdate(dashboard, detailStore, command.workflowId, (detail) => {
        const nextDetail = appendMockAudit(
          {
            ...detail,
            summary: {
              ...detail.summary,
              state: "planned",
            },
          },
          command.actor ?? "operator_ui",
          "resume",
          command.reason ?? "Workflow resumed from mock mode.",
        );
        return nextDetail;
      });
      dashboard = refreshMockDashboardStatistics(dashboard, detailStore);

      return Promise.resolve({ dashboard });
    },

    cancelWorkflow(command) {
      dashboard = applyMockWorkflowUpdate(dashboard, detailStore, command.workflowId, (detail) => {
        const nextDetail = appendMockAudit(
          {
            ...detail,
            summary: {
              ...detail.summary,
              state: "cancelled",
            },
          },
          command.actor ?? "operator_ui",
          "cancel",
          command.reason ?? "Workflow cancelled from mock mode.",
        );
        return nextDetail;
      });
      dashboard = refreshMockDashboardStatistics(dashboard, detailStore);

      return Promise.resolve({ dashboard });
    },

    applyManualIntervention(command) {
      dashboard = applyMockWorkflowUpdate(dashboard, detailStore, command.workflowId, (detail) => {
        let updatedDetail = appendMockAudit(
          detail,
          command.actor ?? "operator_ui",
          "manual_intervention",
          command.note,
        );
        if (command.triggerReorchestration) {
          updatedDetail = appendMockEvent(updatedDetail, "replanning started", "Mock replanning triggered.");
        }
        return updatedDetail;
      });
      dashboard = refreshMockDashboardStatistics(dashboard, detailStore);

      return Promise.resolve({ dashboard });
    },
  };
}

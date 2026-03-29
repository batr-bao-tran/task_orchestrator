import { describe, expect, it, vi } from "vitest";
import { createLiveOperatorClient, createMockOperatorClient, readOperatorClientConfig } from "../../src/lib/operator-client";
import {
  buildOperatorDashboardPayload,
  buildOperatorDashboardUpdatePayload,
  buildOperatorMutationPayload,
} from "../operator-api-payloads";

function readRequestBody(fetchMock: ReturnType<typeof vi.fn<typeof fetch>>, callIndex: number): string {
  const body = fetchMock.mock.calls[callIndex]?.[1]?.body;
  return typeof body === "string" ? body : "";
}

class FakeEventSource {
  readonly listeners = new Map<string, Set<EventListenerOrEventListenerObject>>();
  onopen: ((event: Event) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;
  closed = false;

  constructor(readonly url: string) {}

  addEventListener(type: string, listener: EventListenerOrEventListenerObject) {
    const listeners = this.listeners.get(type) ?? new Set<EventListenerOrEventListenerObject>();
    listeners.add(listener);
    this.listeners.set(type, listeners);
  }

  removeEventListener(type: string, listener: EventListenerOrEventListenerObject) {
    this.listeners.get(type)?.delete(listener);
  }

  close() {
    this.closed = true;
  }

  emit(type: string, data: string) {
    const event = new MessageEvent(type, { data });
    for (const listener of this.listeners.get(type) ?? []) {
      if (typeof listener === "function") {
        listener(event);
      } else {
        listener.handleEvent(event);
      }
    }
  }
}

describe("operator client", () => {
  it("normalizes env-driven client config", () => {
    expect(
      readOperatorClientConfig({
        VITE_OPERATOR_API_BASE_URL: " http://127.0.0.1:8080/ ",
        VITE_OPERATOR_DATA_MODE: "LIVE",
      } as unknown as ImportMetaEnv),
    ).toEqual({
      apiBaseUrl: "http://127.0.0.1:8080/",
      modePreference: "live",
    });

    expect(readOperatorClientConfig({} as unknown as ImportMetaEnv).modePreference).toBe("auto");
  });

  it("maps live dashboard responses into UI data and encodes dashboard queries", async () => {
    const fetchMock = vi.fn<typeof fetch>().mockResolvedValue(
      new Response(JSON.stringify(buildOperatorDashboardPayload())),
    );

    const client = createLiveOperatorClient("http://control-plane/", fetchMock);
    const dashboard = await client.getDashboard({
      selectedWorkflowId: "wf live",
      workflowQuery: "lane blocked",
    });

    expect(fetchMock).toHaveBeenCalledWith(
      "http://control-plane/v1/operator/dashboard?selectedWorkflowId=wf+live&workflowQuery=lane+blocked",
      expect.objectContaining({
        headers: {
          Accept: "application/json",
        },
      }),
    );
    expect(dashboard.workflows).toHaveLength(6);
    expect(dashboard.workflows[0]?.state).toBe("submitted");
    expect(dashboard.workflows[5]?.state).toBe("recovering");
    expect(dashboard.selectedWorkflow?.tasks[0]?.actorDistances[0]?.distance).toBe(4);
    expect(dashboard.selectedWorkflow?.planDiff).toEqual([
      { taskId: "pick-1", after: "robot_1 @ 10:42:00" },
      { taskId: "pick-2", before: "robot_2 @ 10:43:00" },
      { taskId: "pick-3", before: "robot_2 @ 10:44:00", after: "robot_1 @ 10:45:00" },
    ]);
    expect(dashboard.selectedWorkflow?.events[0]?.type).toBe("workflow accepted");
    expect(dashboard.selectedWorkflow?.events[1]?.type).toBe("event");
    expect(dashboard.connectors[1]?.enabled).toBe(false);
  });

  it("subscribes to the live dashboard SSE stream", () => {
    let eventSource: FakeEventSource | undefined;
    const client = createLiveOperatorClient(
      "http://control-plane/",
      vi.fn<typeof fetch>(),
      (url) => {
        eventSource = new FakeEventSource(url);
        return eventSource;
      },
    );
    const onDashboard = vi.fn();
    const onUpdate = vi.fn();
    const onOpen = vi.fn();
    const onError = vi.fn();

    const subscription = client.subscribeDashboard(
      {
        selectedWorkflowId: "wf-live",
        workflowQuery: "lane blocked",
      },
      { onDashboard, onUpdate, onOpen, onError },
    );

    expect(eventSource?.url).toBe(
      "http://control-plane/v1/operator/dashboard:stream?selectedWorkflowId=wf-live&workflowQuery=lane+blocked",
    );

    eventSource?.onopen?.(new Event("open"));
    eventSource?.emit("dashboard", JSON.stringify(buildOperatorDashboardPayload()));
    eventSource?.emit("dashboard-update", JSON.stringify(buildOperatorDashboardUpdatePayload()));
    eventSource?.onerror?.(new Event("error"));

    expect(onOpen).toHaveBeenCalledTimes(1);
    expect(onDashboard).toHaveBeenCalledTimes(1);
    expect(onUpdate).toHaveBeenCalledTimes(1);
    const dashboard = onDashboard.mock.calls[0]?.[0] as { selectedWorkflowId: string } | undefined;
    const update = onUpdate.mock.calls[0]?.[0] as
      | { dashboard: { selectedWorkflowId: string; connectors: { id: string }[] } }
      | undefined;
    expect(dashboard?.selectedWorkflowId).toBe("wf-live");
    expect(update?.dashboard.selectedWorkflowId).toBe("wf-live");
    expect(update?.dashboard.connectors[0]?.id).toBe("webhook");
    expect(onError).toHaveBeenCalledWith(expect.objectContaining({ message: "Live dashboard stream disconnected." }));

    subscription.close();
    expect(eventSource?.closed).toBe(true);
  });

  it("surfaces SSE dashboard update payload failures", () => {
    let eventSource: FakeEventSource | undefined;
    const client = createLiveOperatorClient(
      "http://control-plane/",
      vi.fn<typeof fetch>(),
      (url) => {
        eventSource = new FakeEventSource(url);
        return eventSource;
      },
    );
    const onUpdate = vi.fn();
    const onError = vi.fn();

    const subscription = client.subscribeDashboard({}, { onDashboard: vi.fn(), onUpdate, onError });

    eventSource?.emit("dashboard-update", JSON.stringify({ ok: false, errorMessage: "update rejected" }));

    expect(onUpdate).not.toHaveBeenCalled();
    expect(onError).toHaveBeenCalledWith(expect.objectContaining({ message: "update rejected" }));

    subscription.close();
  });

  it("sends all live mutation requests with the expected payload shape", async () => {
    const fetchMock = vi
      .fn<typeof fetch>()
      .mockImplementation(() => Promise.resolve(new Response(JSON.stringify(buildOperatorMutationPayload()))));
    const client = createLiveOperatorClient("http://control-plane", fetchMock);

    const task = {
      id: "pick-300",
      requestedTimeMs: 1711708920000,
      requestedAt: "29 Mar, 10:42",
      durationMs: 600000,
      durationMinutes: 10,
      latestStartTimeMs: 1711709220000,
      latestStartAt: "29 Mar, 10:47",
      deadlineMs: 1711710720000,
      deadlineAt: "29 Mar, 11:12",
      priority: 4,
      demand: 2,
      mandatory: true,
      preemptible: false,
      allowedActorTypes: ["robot"],
      allowedActorIds: ["robot_1"],
      preferredActorIds: ["robot_2"],
      requiredCapabilities: ["pick"],
      dependencyTaskIds: ["scan-1"],
      mutuallyExclusiveTaskIds: ["pick-9"],
      actorDistances: [{ actorId: "robot_1", distance: 3 }],
      tardinessCostPerUnit: 1.2,
      earlyStartBonus: 0.4,
      phaseDurations: [300000, 300000],
    };
    const actors = [
      {
        id: "robot_1",
        type: "robot",
        capacity: 2,
        capabilities: ["pick"],
      },
    ];

    const upsertWorkflowResult = await client.upsertWorkflow({
      workflowId: "wf-live-clone",
      actors,
      tasks: [task],
      note: "clone workflow",
      actor: "alice",
    });
    const upsertTaskResult = await client.upsertTask({ workflowId: "wf-live", task, note: "reschedule", actor: "alice" });
    const deleteResult = await client.deleteTask({ workflowId: "wf-live", taskId: "pick-300", note: "remove", actor: "alice" });
    const pauseResult = await client.pauseWorkflow({ workflowId: "wf-live", reason: "pause", actor: "alice" });
    const resumeResult = await client.resumeWorkflow({ workflowId: "wf-live", reason: "resume", actor: "alice" });
    const cancelResult = await client.cancelWorkflow({ workflowId: "wf-live", reason: "cancel", actor: "alice" });
    const interventionResult = await client.applyManualIntervention({
      workflowId: "wf-live",
      note: "manual",
      actor: "alice",
      triggerReorchestration: true,
    });

    expect(upsertWorkflowResult.dashboard.selectedWorkflowId).toBe("wf-live");
    expect(upsertTaskResult.dashboard.selectedWorkflowId).toBe("wf-live");
    expect(deleteResult.dashboard.selectedWorkflowId).toBe("wf-live");
    expect(pauseResult.dashboard.selectedWorkflowId).toBe("wf-live");
    expect(resumeResult.dashboard.selectedWorkflowId).toBe("wf-live");
    expect(cancelResult.dashboard.selectedWorkflowId).toBe("wf-live");
    expect(interventionResult.dashboard.selectedWorkflowId).toBe("wf-live");

    expect(fetchMock.mock.calls[0]?.[0]).toBe("http://control-plane/v1/operator/workflows");
    expect(fetchMock.mock.calls[1]?.[0]).toBe("http://control-plane/v1/operator/workflows/wf-live/tasks");
    expect(fetchMock.mock.calls[2]?.[0]).toBe("http://control-plane/v1/operator/workflows/wf-live/tasks/pick-300:delete");
    expect(fetchMock.mock.calls[3]?.[0]).toBe("http://control-plane/v1/operator/workflows/wf-live:pause");
    expect(fetchMock.mock.calls[4]?.[0]).toBe("http://control-plane/v1/operator/workflows/wf-live:resume");
    expect(fetchMock.mock.calls[5]?.[0]).toBe("http://control-plane/v1/operator/workflows/wf-live:cancel");
    expect(fetchMock.mock.calls[6]?.[0]).toBe("http://control-plane/v1/operator/workflows/wf-live:manualIntervention");

    expect(JSON.parse(readRequestBody(fetchMock, 0))).toMatchObject({
      config: {
        id: "wf-live-clone",
        actors: [{ id: "robot_1", type: "robot", capacity: "2", capabilities: ["pick"] }],
        tasks: [{ id: "pick-300", requestedTime: "1711708920000" }],
      },
      note: "clone workflow",
      actor: "alice",
    });
    expect(JSON.parse(readRequestBody(fetchMock, 1))).toMatchObject({
      workflowId: "wf-live",
      note: "reschedule",
      actor: "alice",
      task: {
        id: "pick-300",
        requestedTime: "1711708920000",
        duration: "600000",
        latestStartTime: "1711709220000",
        preferredActorIds: ["robot_2"],
      },
    });
    expect(JSON.parse(readRequestBody(fetchMock, 6))).toEqual({
      workflowId: "wf-live",
      note: "manual",
      actor: "alice",
      triggerReorchestration: true,
    });
  });

  it("surfaces live API and mutation failures cleanly", async () => {
    const client = createLiveOperatorClient(
      "",
      vi
        .fn<typeof fetch>()
        .mockResolvedValueOnce(new Response(JSON.stringify({ errorMessage: "dashboard down" }), { status: 503 }))
        .mockResolvedValueOnce(
          new Response(JSON.stringify(buildOperatorMutationPayload({ ok: false, errorMessage: "manual rejected" }))),
        ),
    );

    await expect(client.getDashboard({})).rejects.toThrow("dashboard down");

    const mutationResult = await client.applyManualIntervention({
      workflowId: "wf-live",
      note: "manual",
      triggerReorchestration: false,
    });
    expect(mutationResult.errorMessage).toBe("manual rejected");
  });

  it("supports search, selection, task mutation, lifecycle updates, and interventions in mock mode", async () => {
    const client = createMockOperatorClient();
    const initialDashboard = await client.getDashboard({});
    const selectedWorkflowId = initialDashboard.selectedWorkflowId;
    const initialTask = initialDashboard.selectedWorkflow?.tasks[0];
    expect(initialTask).toBeDefined();

    const filteredDashboard = await client.getDashboard({ workflowQuery: "robot-recovery" });
    expect(filteredDashboard.workflows).toHaveLength(1);

    const switchedDashboard = await client.getDashboard({ selectedWorkflowId: "robot-recovery-demo" });
    expect(switchedDashboard.selectedWorkflow?.summary.workflowId).toBe("robot-recovery-demo");

    const createdWorkflow = await client.upsertWorkflow({
      workflowId: "warehouse-scenario-copy",
      actors: initialDashboard.selectedWorkflow?.actors ?? [],
      tasks: initialDashboard.selectedWorkflow?.tasks ?? [],
      actor: "operator_ui",
      note: "Create a fresh simulation.",
    });
    expect(createdWorkflow.dashboard.selectedWorkflow?.summary.workflowId).toBe("warehouse-scenario-copy");
    expect(createdWorkflow.dashboard.workflows.some((workflow) => workflow.workflowId === "warehouse-scenario-copy")).toBe(
      true,
    );

    const inserted = await client.upsertTask({
      workflowId: selectedWorkflowId,
      actor: "operator_ui",
      note: "Insert extra order",
      task: {
        ...(initialTask ?? {
          id: "pick-300",
          requestedTimeMs: Date.now(),
          requestedAt: "",
          durationMs: 600000,
          durationMinutes: 10,
          latestStartTimeMs: 0,
          deadlineMs: 0,
          priority: 1,
          mandatory: true,
          preemptible: false,
          allowedActorTypes: [],
          allowedActorIds: [],
          preferredActorIds: [],
          requiredCapabilities: [],
          dependencyTaskIds: [],
          mutuallyExclusiveTaskIds: [],
          actorDistances: [],
          tardinessCostPerUnit: 0,
          earlyStartBonus: 0,
          phaseDurations: [],
        }),
        id: "pick-300",
        preferredActorIds: ["robot_2"],
      },
    });
    expect(inserted.dashboard.selectedWorkflow?.tasks.some((task) => task.id === "pick-300")).toBe(true);

    const deleted = await client.deleteTask({ workflowId: selectedWorkflowId, taskId: "pick-300", actor: "operator_ui" });
    expect(deleted.dashboard.selectedWorkflow?.tasks.some((task) => task.id === "pick-300")).toBe(false);

    const paused = await client.pauseWorkflow({ workflowId: selectedWorkflowId, actor: "operator_ui" });
    expect(paused.dashboard.selectedWorkflow?.summary.state).toBe("paused");

    const resumed = await client.resumeWorkflow({ workflowId: selectedWorkflowId, actor: "operator_ui" });
    expect(resumed.dashboard.selectedWorkflow?.summary.state).toBe("planned");

    const intervention = await client.applyManualIntervention({
      workflowId: selectedWorkflowId,
      actor: "operator_ui",
      note: "Manual override",
      triggerReorchestration: true,
    });
    expect(intervention.dashboard.selectedWorkflow?.audits.at(-1)?.action).toBe("manual_intervention");
    expect(intervention.dashboard.selectedWorkflow?.events.at(-1)?.type).toBe("replanning started");

    const cancelled = await client.cancelWorkflow({ workflowId: selectedWorkflowId, actor: "operator_ui" });
    expect(cancelled.dashboard.selectedWorkflow?.summary.state).toBe("cancelled");
  });
});

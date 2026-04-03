import { act, renderHook, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { useOperatorDashboard } from "../../src/hooks/use-operator-dashboard";
import { buildOperatorDashboardPayload, buildOperatorDashboardUpdatePayload } from "../operator-api-payloads";

class FakeEventSource {
  readonly listeners = new Map<string, Set<EventListenerOrEventListenerObject>>();
  onopen: ((event: Event) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;

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
    // Nothing to release in the test double.
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

describe("useOperatorDashboard", () => {
  afterEach(() => {
    vi.unstubAllGlobals();
    vi.useRealTimers();
  });

  it("loads and mutates dashboard state in mock mode", async () => {
    const config = {
      modePreference: "mock" as const,
      apiBaseUrl: "",
    };
    const { result } = renderHook(() => useOperatorDashboard(config));

    await waitFor(() => {
      expect(result.current.loading).toBe(false);
    });
    expect(result.current.modeLabel).toBe("mock");
    expect(result.current.selectedWorkflowId).toBe("warehouse-morning-wave");

    act(() => {
      result.current.setSearchQuery("robot-recovery");
    });
    await waitFor(() => {
      expect(result.current.dashboard?.workflows[0]?.workflowId).toBe("robot-recovery-demo");
    });

    act(() => {
      result.current.setSearchQuery("");
      result.current.setSelectedWorkflowId("warehouse-morning-wave");
    });
    await act(async () => {
      await result.current.refresh();
    });

    await act(async () => {
      await result.current.upsertWorkflow({
        workflowId: "warehouse-late-wave",
        actors: result.current.dashboard?.selectedWorkflow?.actors ?? [],
        tasks: result.current.dashboard?.selectedWorkflow?.tasks ?? [],
        note: "Create a late wave simulation.",
      });
    });
    expect(result.current.infoMessage).toBe("Saved workflow warehouse-late-wave.");
    expect(result.current.dashboard?.selectedWorkflow?.summary.workflowId).toBe("warehouse-late-wave");

    await act(async () => {
      await result.current.upsertTask({
        workflowId: "warehouse-morning-wave",
        note: "Add late pallet order",
        task: {
          id: "pick-500",
          requestedTimeMs: Date.parse("2026-03-29T15:00:00Z"),
          requestedAt: "29 Mar, 15:00",
          durationMs: 600000,
          durationMinutes: 10,
          latestStartTimeMs: 0,
          deadlineMs: Date.parse("2026-03-29T15:45:00Z"),
          deadlineAt: "29 Mar, 15:45",
          priority: 4,
          mandatory: true,
          preemptible: true,
          allowedActorTypes: [],
          allowedActorIds: [],
          preferredActorIds: ["robot_4"],
          requiredCapabilities: [],
          dependencyTaskIds: [],
          mutuallyExclusiveTaskIds: [],
          actorDistances: [],
          tardinessCostPerUnit: 0,
          earlyStartBonus: 0,
          phaseDurations: [],
        },
      });
    });
    expect(result.current.infoMessage).toBe("Saved task pick-500.");
    expect(result.current.dashboard?.selectedWorkflow?.tasks.some((task) => task.id === "pick-500")).toBe(true);

    await act(async () => {
      await result.current.deleteTask({
        workflowId: "warehouse-morning-wave",
        taskId: "pick-500",
      });
    });
    expect(result.current.infoMessage).toBe("Deleted task pick-500.");
    expect(result.current.dashboard?.selectedWorkflow?.tasks.some((task) => task.id === "pick-500")).toBe(false);

    await act(async () => {
      await result.current.pauseWorkflow({
        workflowId: "warehouse-morning-wave",
      });
    });
    expect(result.current.dashboard?.selectedWorkflow?.summary.state).toBe("paused");

    await act(async () => {
      await result.current.resumeWorkflow({
        workflowId: "warehouse-morning-wave",
      });
    });
    expect(result.current.dashboard?.selectedWorkflow?.summary.state).toBe("planned");

    await act(async () => {
      await result.current.applyManualIntervention({
        workflowId: "warehouse-morning-wave",
        note: "Keep dock lane clear.",
        triggerReorchestration: true,
      });
    });
    expect(result.current.infoMessage).toBe("Recorded a manual intervention for warehouse-morning-wave.");

    await act(async () => {
      await result.current.cancelWorkflow({
        workflowId: "warehouse-morning-wave",
      });
    });
    expect(result.current.dashboard?.selectedWorkflow?.summary.state).toBe("cancelled");
  });

  it("falls back from live mode to mock mode in auto mode", async () => {
    const config = {
      modePreference: "auto" as const,
      apiBaseUrl: "http://control-plane",
    };
    const eventSources: FakeEventSource[] = [];
    vi.stubGlobal(
      "EventSource",
      class extends FakeEventSource {
        constructor(url: string) {
          super(url);
          eventSources.push(this);
        }
      },
    );
    vi.stubGlobal("fetch", vi.fn<typeof fetch>());

    const { result } = renderHook(() => useOperatorDashboard(config));

    act(() => {
      eventSources.at(-1)?.onerror?.(new Event("error"));
    });

    await waitFor(() => {
      expect(result.current.modeLabel).toBe("mock");
      expect(result.current.infoMessage).toBe("Live operator API unavailable, switched to mock mode.");
      expect(result.current.dashboard?.selectedWorkflowId).toBe("warehouse-morning-wave");
    });
  });

  it("subscribes to live updates and refreshes on focus without polling", async () => {
    vi.useFakeTimers();
    const config = {
      modePreference: "live" as const,
      apiBaseUrl: "http://control-plane",
    };
    const eventSources: FakeEventSource[] = [];
    vi.stubGlobal(
      "EventSource",
      class extends FakeEventSource {
        constructor(url: string) {
          super(url);
          eventSources.push(this);
        }
      },
    );
    const fetchMock = vi
      .fn<typeof fetch>()
      .mockImplementation(() => Promise.resolve(new Response(JSON.stringify(buildOperatorDashboardPayload()))));
    vi.stubGlobal("fetch", fetchMock);

    const { result } = renderHook(() => useOperatorDashboard(config));

    await act(async () => {
      eventSources.at(-1)?.emit("dashboard", JSON.stringify(buildOperatorDashboardPayload()));
      await Promise.resolve();
    });
    expect(result.current.loading).toBe(false);
    expect(result.current.modeLabel).toBe("live");
    expect(fetchMock).toHaveBeenCalledTimes(0);
    expect(eventSources.length).toBeGreaterThan(0);

    await act(async () => {
      await vi.advanceTimersByTimeAsync(5_000);
    });
    expect(fetchMock).toHaveBeenCalledTimes(0);

    act(() => {
      eventSources.at(-1)?.onopen?.(new Event("open"));
    });
    expect(result.current.infoMessage).toBe("Live updates connected.");
    expect(result.current.dashboard?.selectedWorkflowId).toBe("wf-live");

    await act(async () => {
      eventSources.at(-1)?.emit("dashboard-update", JSON.stringify(buildOperatorDashboardUpdatePayload()));
      await Promise.resolve();
    });
    expect(result.current.dashboard?.stats.recentEventsPersisted).toBe(13);
    expect(fetchMock).toHaveBeenCalledTimes(0);

    act(() => {
      eventSources.at(-1)?.onerror?.(new Event("error"));
    });
    expect(result.current.connectionInterrupted).toBe(true);
    expect(result.current.errorMessage).toBe("We can't reach live updates right now. Please refresh and try again in a moment.");
    expect(result.current.infoMessage).toBeUndefined();

    await act(async () => {
      window.dispatchEvent(new Event("focus"));
      await Promise.resolve();
    });
    expect(fetchMock).toHaveBeenCalledTimes(1);

    await act(async () => {
      document.dispatchEvent(new Event("visibilitychange"));
      await Promise.resolve();
    });
    expect(fetchMock).toHaveBeenCalledTimes(2);
  });

  it("surfaces live errors when fallback is disabled", async () => {
    const config = {
      modePreference: "live" as const,
      apiBaseUrl: "http://control-plane",
    };
    const eventSources: FakeEventSource[] = [];
    vi.stubGlobal(
      "EventSource",
      class extends FakeEventSource {
        constructor(url: string) {
          super(url);
          eventSources.push(this);
        }
      },
    );
    vi.stubGlobal("fetch", vi.fn<typeof fetch>());

    const { result } = renderHook(() => useOperatorDashboard(config));

    act(() => {
      eventSources.at(-1)?.onerror?.(new Event("error"));
    });

    await waitFor(() => {
      expect(result.current.loading).toBe(false);
    });
    expect(result.current.modeLabel).toBe("live");
    expect(result.current.connectionInterrupted).toBe(true);
    expect(result.current.errorMessage).toBe("Live dashboard stream disconnected.");
  });

  it("surfaces thrown mutation failures in live mode", async () => {
    const config = {
      modePreference: "live" as const,
      apiBaseUrl: "http://control-plane",
    };
    const eventSources: FakeEventSource[] = [];
    vi.stubGlobal(
      "EventSource",
      class extends FakeEventSource {
        constructor(url: string) {
          super(url);
          eventSources.push(this);
        }
      },
    );
    const fetchMock = vi.fn<typeof fetch>().mockRejectedValueOnce("boom");
    vi.stubGlobal("fetch", fetchMock);

    const { result } = renderHook(() => useOperatorDashboard(config));

    await act(async () => {
      eventSources.at(-1)?.emit("dashboard", JSON.stringify(buildOperatorDashboardPayload()));
      await Promise.resolve();
    });

    await act(async () => {
      await result.current.pauseWorkflow({
        workflowId: "wf-live",
      });
    });
    expect(result.current.errorMessage).toBe("Operator mutation failed.");
    expect(result.current.mutating).toBe(false);
  });
});

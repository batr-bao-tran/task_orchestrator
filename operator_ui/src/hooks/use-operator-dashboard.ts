import { startTransition, useDeferredValue, useEffect, useEffectEvent, useRef, useState } from "react";
import {
  createLiveOperatorClient,
  createMockOperatorClient,
  readOperatorClientConfig,
  type DashboardStreamUpdate,
  type OperatorClientConfig,
} from "../lib/operator-client";
import type {
  DashboardQuery,
  DeleteTaskCommand,
  ManualInterventionCommand,
  MutationResult,
  OperatorDashboard,
  WorkflowActionCommand,
  WorkflowUpsertCommand,
  WorkflowTaskMutation,
} from "../lib/types";

type DeleteTaskAlias = DeleteTaskCommand;

interface UseOperatorDashboardState {
  dashboard?: OperatorDashboard;
  selectedWorkflowId: string;
  searchQuery: string;
  loading: boolean;
  mutating: boolean;
  errorMessage?: string;
  infoMessage?: string;
  connectionInterrupted: boolean;
  modeLabel: "live" | "mock";
  setSelectedWorkflowId: (workflowId: string) => void;
  setSearchQuery: (query: string) => void;
  refresh: () => Promise<void>;
  upsertWorkflow: (command: WorkflowUpsertCommand) => Promise<void>;
  upsertTask: (command: WorkflowTaskMutation) => Promise<void>;
  deleteTask: (command: DeleteTaskAlias) => Promise<void>;
  pauseWorkflow: (command: WorkflowActionCommand) => Promise<void>;
  resumeWorkflow: (command: WorkflowActionCommand) => Promise<void>;
  cancelWorkflow: (command: WorkflowActionCommand) => Promise<void>;
  applyManualIntervention: (command: ManualInterventionCommand) => Promise<void>;
}

function makeQuery(selectedWorkflowId: string, workflowQuery: string): DashboardQuery {
  return {
    selectedWorkflowId: selectedWorkflowId.length > 0 ? selectedWorkflowId : undefined,
    workflowQuery: workflowQuery.length > 0 ? workflowQuery : undefined,
  };
}

export function useOperatorDashboard(config: OperatorClientConfig = readOperatorClientConfig(import.meta.env)) {
  const [liveClient] = useState(() => createLiveOperatorClient(config.apiBaseUrl));
  const [mockClient] = useState(() => createMockOperatorClient());
  const [activeMode, setActiveMode] = useState<"live" | "mock">(config.modePreference === "mock" ? "mock" : "live");
  const [dashboard, setDashboard] = useState<OperatorDashboard>();
  const [selectedWorkflowId, setSelectedWorkflowIdState] = useState("");
  const [searchQuery, setSearchQueryState] = useState("");
  const [loading, setLoading] = useState(true);
  const [mutating, setMutating] = useState(false);
  const [errorMessage, setErrorMessage] = useState<string>();
  const [infoMessage, setInfoMessage] = useState<string>();
  const [connectionInterrupted, setConnectionInterrupted] = useState(false);
  const deferredSearchQuery = useDeferredValue(searchQuery);
  const mountedRef = useRef(true);
  const hasLiveSnapshotRef = useRef(false);
  const fallbackInProgressRef = useRef(false);
  const skipNextLoadRef = useRef(false);
  const isMounted = useEffectEvent(() => mountedRef.current);

  useEffect(() => {
    mountedRef.current = true;

    return () => {
      mountedRef.current = false;
    };
  }, []);

  const resolveClient = useEffectEvent(() => (activeMode === "mock" ? mockClient : liveClient));
  const applyDashboard = useEffectEvent((nextDashboard: OperatorDashboard) => {
    if (!isMounted()) {
      return;
    }
    hasLiveSnapshotRef.current = true;
    fallbackInProgressRef.current = false;
    startTransition(() => {
      setDashboard(nextDashboard);
      setConnectionInterrupted(false);
      setErrorMessage(undefined);
      setLoading(false);
    });
  });

  const applyDashboardUpdate = useEffectEvent((update: DashboardStreamUpdate) => {
    if (!isMounted()) {
      return;
    }
    startTransition(() => {
      setDashboard((currentDashboard) => {
        if (currentDashboard === undefined) {
          return currentDashboard;
        }

        return {
          ...currentDashboard,
          serverTimeUnixMs: update.dashboard.serverTimeUnixMs,
          serverTime: update.dashboard.serverTime,
          stats: update.dashboard.stats,
          workflows: update.dashboard.workflows,
          connectors: update.dashboard.connectors,
          selectedWorkflowId:
            update.dashboard.selectedWorkflowId.length > 0
              ? update.dashboard.selectedWorkflowId
              : currentDashboard.selectedWorkflowId,
          selectedWorkflow: update.dashboard.selectedWorkflow ?? currentDashboard.selectedWorkflow,
        };
      });
      setConnectionInterrupted(false);
      setErrorMessage(undefined);
      setLoading(false);
    });
  });

  const fallbackToMock = useEffectEvent(async () => {
    if (fallbackInProgressRef.current) {
      return;
    }
    fallbackInProgressRef.current = true;
    skipNextLoadRef.current = true;
    setActiveMode("mock");
    const fallbackDashboard = await mockClient.getDashboard(makeQuery(selectedWorkflowId, deferredSearchQuery));
    if (!isMounted()) {
      return;
    }
    hasLiveSnapshotRef.current = false;
    startTransition(() => {
      setDashboard(fallbackDashboard);
      setConnectionInterrupted(false);
      setErrorMessage(undefined);
      setInfoMessage("Live operator API unavailable, switched to mock mode.");
      setLoading(false);
    });
  });

  const loadDashboard = useEffectEvent(async () => {
    setLoading(true);
    setConnectionInterrupted(false);
    setErrorMessage(undefined);

    try {
      const nextDashboard = await resolveClient().getDashboard(makeQuery(selectedWorkflowId, deferredSearchQuery));
      applyDashboard(nextDashboard);
      if (activeMode === "live") {
        setInfoMessage("Live control plane connected.");
      }
    } catch (error) {
      if (config.modePreference === "auto" && activeMode === "live") {
        await fallbackToMock();
      } else {
        if (isMounted()) {
          setErrorMessage(error instanceof Error ? error.message : "Failed to load the operator dashboard.");
        }
      }
    } finally {
      if (isMounted()) {
        setLoading(false);
      }
    }
  });

  useEffect(() => {
    if (activeMode === "live") {
      return;
    }
    if (skipNextLoadRef.current) {
      skipNextLoadRef.current = false;
      return;
    }
    void loadDashboard();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [activeMode, deferredSearchQuery, selectedWorkflowId]);

  useEffect(() => {
    if (activeMode !== "live") {
      return undefined;
    }

    hasLiveSnapshotRef.current = false;
    fallbackInProgressRef.current = false;
    setLoading(true);
    setConnectionInterrupted(false);
    setErrorMessage(undefined);

    const subscription = liveClient.subscribeDashboard(makeQuery(selectedWorkflowId, deferredSearchQuery), {
      onDashboard(nextDashboard) {
        applyDashboard(nextDashboard);
      },
      onUpdate(update) {
        applyDashboardUpdate(update);
      },
      onOpen() {
        if (isMounted()) {
          setConnectionInterrupted(false);
          setErrorMessage(undefined);
          setInfoMessage("Live updates connected.");
        }
      },
      onError(error) {
        if (!isMounted()) {
          return;
        }

        if (!hasLiveSnapshotRef.current) {
          if (config.modePreference === "auto") {
            void fallbackToMock();
            return;
          }
          setConnectionInterrupted(true);
          setErrorMessage(error.message);
          setLoading(false);
          return;
        }

        setConnectionInterrupted(true);
        setErrorMessage("We can't reach live updates right now. Please refresh and try again in a moment.");
        setInfoMessage(undefined);
        setLoading(false);
      },
    });

    const refreshOnFocus = () => {
      if (hasLiveSnapshotRef.current && document.visibilityState === "visible") {
        void loadDashboard();
      }
    };

    window.addEventListener("focus", refreshOnFocus);
    document.addEventListener("visibilitychange", refreshOnFocus);

    return () => {
      subscription.close();
      window.removeEventListener("focus", refreshOnFocus);
      document.removeEventListener("visibilitychange", refreshOnFocus);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [activeMode, deferredSearchQuery, selectedWorkflowId]);

  const runMutation = useEffectEvent(
    async (
        mutate: () => Promise<MutationResult>,
        successMessage: string,
      ) => {
      setMutating(true);
      setErrorMessage(undefined);
      setInfoMessage(undefined);

      try {
        const result = await mutate();
        if (!isMounted()) {
          return;
        }
        startTransition(() => {
          setDashboard(result.dashboard);
          setSelectedWorkflowIdState(result.dashboard.selectedWorkflowId);
          if (result.errorMessage) {
            setErrorMessage(result.errorMessage);
          } else {
            setInfoMessage(successMessage);
          }
        });
      } catch (error) {
        if (isMounted()) {
          setErrorMessage(error instanceof Error ? error.message : "Operator mutation failed.");
        }
      } finally {
        if (isMounted()) {
          setMutating(false);
        }
      }
    },
  );

  const state: UseOperatorDashboardState = {
    dashboard,
    selectedWorkflowId: dashboard?.selectedWorkflowId ?? selectedWorkflowId,
    searchQuery,
    loading,
    mutating,
    errorMessage,
    infoMessage,
    connectionInterrupted,
    modeLabel: activeMode,
    setSelectedWorkflowId(workflowId) {
      setSelectedWorkflowIdState(workflowId);
    },
    setSearchQuery(query) {
      setSearchQueryState(query);
    },
    refresh: async () => {
      await loadDashboard();
    },
    upsertWorkflow: async (command) => {
      await runMutation(() => resolveClient().upsertWorkflow(command), `Saved workflow ${command.workflowId}.`);
    },
    upsertTask: async (command) => {
      await runMutation(() => resolveClient().upsertTask(command), `Saved task ${command.task.id}.`);
    },
    deleteTask: async (command) => {
      await runMutation(() => resolveClient().deleteTask(command), `Deleted task ${command.taskId}.`);
    },
    pauseWorkflow: async (command) => {
      await runMutation(() => resolveClient().pauseWorkflow(command), `Paused workflow ${command.workflowId}.`);
    },
    resumeWorkflow: async (command) => {
      await runMutation(() => resolveClient().resumeWorkflow(command), `Resumed workflow ${command.workflowId}.`);
    },
    cancelWorkflow: async (command) => {
      await runMutation(() => resolveClient().cancelWorkflow(command), `Cancelled workflow ${command.workflowId}.`);
    },
    applyManualIntervention: async (command) => {
      await runMutation(
        () => resolveClient().applyManualIntervention(command),
        `Recorded a manual intervention for ${command.workflowId}.`,
      );
    },
  };

  return state;
}

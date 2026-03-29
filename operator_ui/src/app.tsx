import { IntegrationPanel } from "./components/integration-panel";
import { TaskSimulator } from "./components/task-simulator";
import { WorkflowControls } from "./components/workflow-controls";
import { WorkflowDetail } from "./components/workflow-detail";
import { WorkflowScenarioForm } from "./components/workflow-scenario-form";
import { WorkflowTable } from "./components/workflow-table";
import { useOperatorDashboard } from "./hooks/use-operator-dashboard";

export function App() {
  const {
    dashboard,
    selectedWorkflowId,
    searchQuery,
    loading,
    mutating,
    errorMessage,
    infoMessage,
    modeLabel,
    setSelectedWorkflowId,
    setSearchQuery,
    refresh,
    upsertWorkflow,
    upsertTask,
    deleteTask,
    pauseWorkflow,
    resumeWorkflow,
    cancelWorkflow,
    applyManualIntervention,
  } = useOperatorDashboard();

  return (
    <main className="shell">
      <section className="hero">
        <div>
          <p className="eyebrow">Task Orchestrator</p>
          <h1>Operate the durable control plane in real time.</h1>
          <p className="hero-copy">
            Inspect live workflow history, pause or recover plans, and simulate incoming orders by inserting,
            rescheduling, or deleting tasks directly from the operator console.
          </p>
        </div>
        <div className="hero-stats">
          <div>
            <span>{dashboard?.stats.recentEventsPersisted ?? 0}</span>
            <p>events in the last 24h</p>
          </div>
          <div>
            <span>{dashboard?.stats.planVersionsRetained ?? 0}</span>
            <p>plan versions retained</p>
          </div>
          <div>
            <span>{dashboard?.stats.activeWorkflows ?? 0}</span>
            <p>active workflows</p>
          </div>
          <div>
            <span>{dashboard?.stats.connectorsTracked ?? 0}</span>
            <p>connectors tracked</p>
          </div>
        </div>
      </section>

      <section className="panel toolbar-panel">
        <label className="field search-field">
          <span>Search workflows</span>
          <input
            onChange={(event) => {
              setSearchQuery(event.target.value);
            }}
            placeholder="workflow id or recent error"
            value={searchQuery}
          />
        </label>

        <div className="toolbar-actions">
          <span className={`pill ${modeLabel === "mock" ? "pill--paused" : "pill--enabled"}`}>
            {modeLabel === "mock" ? "mock mode" : "live mode"}
          </span>
          <button className="action-button" disabled={loading || mutating} onClick={() => void refresh()} type="button">
            Refresh
          </button>
        </div>
      </section>

      {errorMessage ? (
        <section className="status-banner status-banner--error" role="alert">
          {errorMessage}
        </section>
      ) : null}

      {infoMessage ? <section className="status-banner status-banner--info">{infoMessage}</section> : null}

      <section className="layout">
        <WorkflowTable
          onSelect={setSelectedWorkflowId}
          selectedWorkflowId={dashboard?.selectedWorkflowId ?? selectedWorkflowId}
          workflows={dashboard?.workflows ?? []}
        />

        <div className="workspace">
          <WorkflowScenarioForm
            busy={mutating}
            detail={dashboard?.selectedWorkflow}
            onCreateWorkflow={upsertWorkflow}
          />
          <WorkflowDetail detail={dashboard?.selectedWorkflow} />
          <WorkflowControls
            busy={mutating}
            detail={dashboard?.selectedWorkflow}
            onCancel={cancelWorkflow}
            onManualIntervention={applyManualIntervention}
            onPause={pauseWorkflow}
            onResume={resumeWorkflow}
          />
          <TaskSimulator
            busy={mutating}
            detail={dashboard?.selectedWorkflow}
            onDeleteTask={deleteTask}
            onSaveTask={upsertTask}
          />
        </div>
      </section>

      <IntegrationPanel connectors={dashboard?.connectors ?? []} />
    </main>
  );
}

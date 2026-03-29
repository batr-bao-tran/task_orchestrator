import type { WorkflowSummary } from "../lib/types";

interface WorkflowTableProps {
  workflows: WorkflowSummary[];
  selectedWorkflowId: string;
  onSelect: (workflowId: string) => void;
}

export function WorkflowTable({ workflows, selectedWorkflowId, onSelect }: WorkflowTableProps) {
  return (
    <section className="panel">
      <div className="panel-header">
        <div>
          <p className="eyebrow">Lifecycle</p>
          <h2>Durable workflows</h2>
        </div>
        <span className="pill">{workflows.length} tracked</span>
      </div>
      <div className="workflow-list">
        {workflows.length === 0 ? (
          <p className="muted">No workflows match the current filter.</p>
        ) : (
          workflows.map((workflow) => (
            <button
              className={`workflow-row ${workflow.workflowId === selectedWorkflowId ? "workflow-row--active" : ""}`}
              key={workflow.workflowId}
              onClick={() => {
                onSelect(workflow.workflowId);
              }}
              type="button"
            >
              <div>
                <strong>{workflow.workflowId}</strong>
                <span className={`state state--${workflow.state}`}>{workflow.state}</span>
              </div>
              <div className="workflow-meta">
                <span>v{workflow.latestPlanVersion}</span>
                <span>{workflow.totalEvents} events</span>
                <span>{workflow.updatedAt}</span>
              </div>
              {workflow.lastError ? <p className="workflow-error">{workflow.lastError}</p> : null}
            </button>
          ))
        )}
      </div>
    </section>
  );
}

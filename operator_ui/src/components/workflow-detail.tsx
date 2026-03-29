import type { WorkflowDetail as WorkflowDetailModel } from "../lib/types";

interface WorkflowDetailProps {
  detail?: WorkflowDetailModel;
}

export function WorkflowDetail({ detail }: WorkflowDetailProps) {
  if (detail === undefined) {
    return (
      <section className="detail-grid">
        <article className="panel empty-panel">
          <p className="eyebrow">Workflow</p>
          <h2>No workflow selected</h2>
          <p className="muted">Choose a workflow from the left to inspect its plan history, tasks, and audit trail.</p>
        </article>
      </section>
    );
  }

  return (
    <section className="detail-grid">
      <article className="panel">
        <div className="panel-header">
          <div>
            <p className="eyebrow">Workflow</p>
            <h2>{detail.summary.workflowId}</h2>
          </div>
          <span className={`pill pill--status pill--${detail.summary.state}`}>{detail.summary.state}</span>
        </div>
        <dl className="summary-grid">
          <div>
            <dt>Plan version</dt>
            <dd>{detail.summary.latestPlanVersion}</dd>
          </div>
          <div>
            <dt>Events</dt>
            <dd>{detail.summary.totalEvents}</dd>
          </div>
          <div>
            <dt>Actors</dt>
            <dd>{detail.actors.length}</dd>
          </div>
          <div>
            <dt>Orders</dt>
            <dd>{detail.tasks.length}</dd>
          </div>
          <div>
            <dt>Updated</dt>
            <dd>{detail.summary.updatedAt}</dd>
          </div>
          <div>
            <dt>Operator note</dt>
            <dd>{detail.operatorsNote}</dd>
          </div>
        </dl>

        <div className="subpanel">
          <div className="panel-header">
            <div>
              <p className="eyebrow">Latest plan</p>
              <h3>Assignments</h3>
            </div>
            <span className="pill">{detail.assignments.length} assigned</span>
          </div>
          <div className="assignment-list">
            {detail.assignments.length === 0 ? (
              <p className="muted">No current assignments are stored for this workflow.</p>
            ) : (
              detail.assignments.map((assignment) => (
                <div className="assignment-row" key={`${assignment.taskId}-${assignment.actorId}`}>
                  <strong>{assignment.taskId}</strong>
                  <span>{assignment.actorId}</span>
                  <span>
                    {assignment.startAt} to {assignment.endAt}
                  </span>
                </div>
              ))
            )}
          </div>
        </div>
      </article>

      <article className="panel">
        <div className="panel-header">
          <div>
            <p className="eyebrow">Plan history</p>
            <h2>Assignment diff</h2>
          </div>
          <span className="pill">{detail.planDiff.length} changes</span>
        </div>
        <div className="diff-list">
          {detail.planDiff.length === 0 ? (
            <p className="muted">No assignment diff recorded for this version transition.</p>
          ) : (
            detail.planDiff.map((change) => (
              <div
                className="diff-row"
                key={`${change.taskId}-${change.before ?? "new"}-${change.after ?? "removed"}`}
              >
                <strong>{change.taskId}</strong>
                <span>{change.before ?? "new"}</span>
                <span className="diff-arrow">→</span>
                <span>{change.after ?? "removed"}</span>
              </div>
            ))
          )}
        </div>
      </article>

      <article className="panel">
        <div className="panel-header">
          <div>
            <p className="eyebrow">History</p>
            <h2>Event timeline</h2>
          </div>
          <span className="pill">{detail.events.length} recent</span>
        </div>
        <div className="timeline">
          {detail.events.map((event) => (
            <div className="timeline-row" key={`${String(event.sequence)}-${event.type}`}>
              <div className="timeline-sequence">{event.sequence}</div>
              <div>
                <strong>{event.type}</strong>
                <p>{event.detail}</p>
              </div>
              <time>{event.recordedAt}</time>
            </div>
          ))}
        </div>
      </article>

      <article className="panel">
        <div className="panel-header">
          <div>
            <p className="eyebrow">Audit</p>
            <h2>Operator actions</h2>
          </div>
          <span className="pill">{detail.audits.length} entries</span>
        </div>
        <div className="timeline">
          {detail.audits.length === 0 ? (
            <p className="muted">No audit entries are stored yet for this workflow.</p>
          ) : (
            detail.audits.map((entry) => (
              <div className="timeline-row" key={`${String(entry.sequence)}-${entry.action}`}>
                <div className="timeline-sequence">{entry.sequence}</div>
                <div>
                  <strong>{entry.action}</strong>
                  <p>
                    {entry.actor}: {entry.detail}
                  </p>
                </div>
                <time>{entry.recordedAt}</time>
              </div>
            ))
          )}
        </div>
      </article>
    </section>
  );
}

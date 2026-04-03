import type { WorkflowDetail as WorkflowDetailModel } from "../lib/types";
import { GanttTimeline } from "./gantt-timeline";
import { PaginatedList } from "./paginated-list";

interface WorkflowHistoryProps {
  detail?: WorkflowDetailModel;
}

export function EventTimeline({ detail }: WorkflowHistoryProps) {
  if (detail === undefined) {
    return (
      <article className="panel empty-panel">
        <p className="eyebrow">History</p>
        <h2>No workflow selected</h2>
        <p className="muted">Choose a workflow from the left to view its event timeline.</p>
      </article>
    );
  }

  return (
    <article className="panel">
      <div className="panel-header">
        <div>
          <p className="eyebrow">History</p>
          <h2>Event timeline</h2>
        </div>
        <span className="pill">{detail.events.length} recent</span>
      </div>
      <div className="timeline">
        <PaginatedList
          items={detail.events}
          keyExtractor={(event) => `${String(event.sequence)}-${event.type}`}
          emptyMessage="No events recorded yet for this workflow."
          renderItem={(event) => (
            <div className="timeline-row">
              <div className="timeline-sequence">{event.sequence}</div>
              <div>
                <strong>{event.type}</strong>
                <p>{event.detail}</p>
              </div>
              <time>{event.recordedAt}</time>
            </div>
          )}
        />
      </div>
    </article>
  );
}

export function AuditTrail({ detail }: WorkflowHistoryProps) {
  if (detail === undefined) {
    return (
      <article className="panel empty-panel">
        <p className="eyebrow">Audit</p>
        <h2>No workflow selected</h2>
        <p className="muted">Choose a workflow from the left to view its audit trail.</p>
      </article>
    );
  }

  return (
    <article className="panel">
      <div className="panel-header">
        <div>
          <p className="eyebrow">Audit</p>
          <h2>Operator actions</h2>
        </div>
        <span className="pill">{detail.audits.length} entries</span>
      </div>
      <div className="timeline">
        <PaginatedList
          items={detail.audits}
          keyExtractor={(entry) => `${String(entry.sequence)}-${entry.action}`}
          emptyMessage="No audit entries are stored yet for this workflow."
          renderItem={(entry) => (
            <div className="timeline-row">
              <div className="timeline-sequence">{entry.sequence}</div>
              <div>
                <strong>{entry.action}</strong>
                <p>
                  {entry.actor}: {entry.detail}
                </p>
              </div>
              <time>{entry.recordedAt}</time>
            </div>
          )}
        />
      </div>
    </article>
  );
}

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
              <h3>Schedule</h3>
            </div>
            <span className="pill">{detail.assignments.length} assigned</span>
          </div>
          <GanttTimeline
            key={`${detail.summary.workflowId}-${String(detail.summary.latestPlanVersion)}`}
            assignments={detail.assignments}
            actors={detail.actors}
            tasks={detail.tasks}
            workflowState={detail.summary.state}
          />
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
          <PaginatedList
            items={detail.planDiff}
            keyExtractor={(change) => `${change.taskId}-${change.before ?? "new"}-${change.after ?? "removed"}`}
            emptyMessage="No assignment diff recorded for this version transition."
            renderItem={(change) => (
              <div className="diff-row">
                <strong>{change.taskId}</strong>
                <span>{change.before ?? "new"}</span>
                <span className="diff-arrow">→</span>
                <span>{change.after ?? "removed"}</span>
              </div>
            )}
          />
        </div>
      </article>

      <EventTimeline detail={detail} />
      <AuditTrail detail={detail} />
    </section>
  );
}

import { useEffect, useState } from "react";
import { formatDateTime, fromDatetimeLocalValue, toDatetimeLocalValue } from "../lib/date-time";
import type { WorkflowDetail, WorkflowTask, WorkflowUpsertCommand } from "../lib/types";

interface WorkflowScenarioFormProps {
  detail?: WorkflowDetail;
  busy: boolean;
  onCreateWorkflow: (command: WorkflowUpsertCommand) => Promise<void>;
}

function resolveScenarioAnchor(detail: WorkflowDetail | undefined): string {
  const earliestTaskStart = detail?.tasks.reduce<number | undefined>(
    (currentEarliest, task) =>
      currentEarliest === undefined ? task.requestedTimeMs : Math.min(currentEarliest, task.requestedTimeMs),
    undefined,
  );
  return toDatetimeLocalValue(earliestTaskStart ?? Date.now());
}

function shiftTaskSchedule(task: WorkflowTask, offsetMs: number): WorkflowTask {
  const requestedTimeMs = task.requestedTimeMs + offsetMs;
  const latestStartTimeMs = task.latestStartTimeMs > 0 ? task.latestStartTimeMs + offsetMs : 0;
  const deadlineMs = task.deadlineMs > 0 ? task.deadlineMs + offsetMs : 0;

  return {
    ...task,
    requestedTimeMs,
    requestedAt: formatDateTime(requestedTimeMs),
    latestStartTimeMs,
    latestStartAt: latestStartTimeMs > 0 ? formatDateTime(latestStartTimeMs) : undefined,
    deadlineMs,
    deadlineAt: deadlineMs > 0 ? formatDateTime(deadlineMs) : undefined,
  };
}

export function WorkflowScenarioForm({ detail, busy, onCreateWorkflow }: WorkflowScenarioFormProps) {
  const [workflowId, setWorkflowId] = useState("");
  const [cloneCurrentTasks, setCloneCurrentTasks] = useState(true);
  const [scenarioAnchor, setScenarioAnchor] = useState(resolveScenarioAnchor(detail));
  const [note, setNote] = useState("");

  useEffect(() => {
    setWorkflowId("");
    setCloneCurrentTasks(true);
    setScenarioAnchor(resolveScenarioAnchor(detail));
    setNote("");
  }, [detail]);

  if (detail === undefined) {
    return (
      <section className="panel scenario-panel">
        <div className="panel-header">
          <div>
            <p className="eyebrow">Scenario</p>
            <h2>Create a workflow simulation</h2>
          </div>
          <span className="pill">select a template</span>
        </div>
        <p className="muted">
          Choose a workflow first, then create a new simulation that copies its actor pool and optionally shifts its
          task schedule to a new start time.
        </p>
      </section>
    );
  }

  const templateStart = detail.tasks.reduce<number | undefined>(
    (currentEarliest, task) =>
      currentEarliest === undefined ? task.requestedTimeMs : Math.min(currentEarliest, task.requestedTimeMs),
    undefined,
  );
  const taskCountLabel = String(detail.tasks.length);

  return (
    <section className="panel scenario-panel">
      <div className="panel-header">
        <div>
          <p className="eyebrow">Scenario</p>
          <h2>Create a workflow simulation</h2>
        </div>
        <span className="pill">{detail.actors.length} actors copied</span>
      </div>

      <p className="muted">
        Create a new workflow using the current actor pool. You can clone the current task schedule and shift it to a
        new live start time, or create an empty shell and add new orders afterwards.
      </p>

      <form
        className="task-form"
        onSubmit={(event) => {
          event.preventDefault();

          const offsetMs =
            cloneCurrentTasks && templateStart !== undefined ? fromDatetimeLocalValue(scenarioAnchor) - templateStart : 0;
          const tasks = cloneCurrentTasks ? detail.tasks.map((task) => shiftTaskSchedule(task, offsetMs)) : [];

          void onCreateWorkflow({
            workflowId: workflowId.trim(),
            actors: detail.actors,
            tasks,
            note: note.trim().length > 0 ? note.trim() : undefined,
          }).then(() => {
            setWorkflowId("");
            setNote("");
          });
        }}
      >
        <div className="form-grid">
          <label className="field">
            <span>New workflow ID</span>
            <input
              disabled={busy}
              onChange={(event) => {
                setWorkflowId(event.target.value);
              }}
              placeholder={`${detail.summary.workflowId}-simulation`}
              required
              value={workflowId}
            />
          </label>

          <label className="field">
            <span>Simulation start time</span>
            <input
              disabled={busy || !cloneCurrentTasks}
              onChange={(event) => {
                setScenarioAnchor(event.target.value);
              }}
              required={cloneCurrentTasks}
              type="datetime-local"
              value={scenarioAnchor}
            />
          </label>
        </div>

        <label className="checkbox-field">
          <input
            checked={cloneCurrentTasks}
            disabled={busy}
            onChange={(event) => {
              setCloneCurrentTasks(event.target.checked);
            }}
            type="checkbox"
          />
          Clone the current task schedule into the new workflow
        </label>

        <label className="field">
          <span>Scenario note</span>
          <textarea
            disabled={busy}
            onChange={(event) => {
              setNote(event.target.value);
            }}
            placeholder="Describe the condition you want to simulate, such as a new wave, a delayed shift, or a dock outage."
            rows={3}
            value={note}
          />
        </label>

        <div className="toolbar-actions">
          <button
            className="action-button action-button--primary"
            disabled={busy || workflowId.trim().length === 0}
            type="submit"
          >
            Create workflow
          </button>
          <span className="muted scenario-meta">
            {cloneCurrentTasks
              ? `Clones ${taskCountLabel} task${detail.tasks.length === 1 ? "" : "s"} from ${detail.summary.workflowId}`
              : "Starts with an empty task board so you can add orders manually."}
          </span>
        </div>
      </form>
    </section>
  );
}

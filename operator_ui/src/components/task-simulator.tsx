import { useEffect, useState } from "react";
import {
  fromDatetimeLocalValue,
  formatDateTime,
  minutesToMilliseconds,
  toDatetimeLocalValue,
} from "../lib/date-time";
import type { DeleteTaskCommand, TaskEditorDraft, WorkflowDetail, WorkflowTask, WorkflowTaskMutation } from "../lib/types";

interface TaskSimulatorProps {
  detail?: WorkflowDetail;
  busy: boolean;
  onSaveTask: (command: WorkflowTaskMutation) => Promise<void>;
  onDeleteTask: (command: DeleteTaskCommand) => Promise<void>;
}

function makeEmptyDraft(): TaskEditorDraft {
  return {
    id: "",
    requestedAt: "",
    deadlineAt: "",
    durationMinutes: 15,
    priority: 1,
    preferredActorId: "",
    mandatory: true,
    preemptible: false,
    note: "",
  };
}

function makeDraftFromTask(task: WorkflowTask): TaskEditorDraft {
  return {
    id: task.id,
    requestedAt: toDatetimeLocalValue(task.requestedTimeMs),
    deadlineAt: toDatetimeLocalValue(task.deadlineMs),
    durationMinutes: task.durationMinutes,
    priority: task.priority,
    preferredActorId: task.preferredActorIds[0] ?? "",
    mandatory: task.mandatory,
    preemptible: task.preemptible,
    note: "",
  };
}

function mergeTaskDraft(existingTask: WorkflowTask | undefined, draft: TaskEditorDraft): WorkflowTask {
  const requestedTimeMs = fromDatetimeLocalValue(draft.requestedAt);
  const deadlineMs = fromDatetimeLocalValue(draft.deadlineAt);

  return {
    id: draft.id.trim(),
    requestedTimeMs,
    requestedAt: formatDateTime(requestedTimeMs),
    durationMs: minutesToMilliseconds(draft.durationMinutes),
    durationMinutes: draft.durationMinutes,
    latestStartTimeMs: existingTask?.latestStartTimeMs ?? 0,
    latestStartAt:
      existingTask?.latestStartTimeMs && existingTask.latestStartTimeMs > 0
        ? formatDateTime(existingTask.latestStartTimeMs)
        : undefined,
    deadlineMs,
    deadlineAt: deadlineMs > 0 ? formatDateTime(deadlineMs) : undefined,
    priority: draft.priority,
    demand: existingTask?.demand,
    mandatory: draft.mandatory,
    preemptible: draft.preemptible,
    allowedActorTypes: existingTask?.allowedActorTypes ?? [],
    allowedActorIds: existingTask?.allowedActorIds ?? [],
    preferredActorIds: draft.preferredActorId.length > 0 ? [draft.preferredActorId] : [],
    requiredCapabilities: existingTask?.requiredCapabilities ?? [],
    dependencyTaskIds: existingTask?.dependencyTaskIds ?? [],
    mutuallyExclusiveTaskIds: existingTask?.mutuallyExclusiveTaskIds ?? [],
    actorDistances: existingTask?.actorDistances ?? [],
    tardinessCostPerUnit: existingTask?.tardinessCostPerUnit ?? 0,
    earlyStartBonus: existingTask?.earlyStartBonus ?? 0,
    phaseDurations: existingTask?.phaseDurations ?? [],
  };
}

export function TaskSimulator({ detail, busy, onSaveTask, onDeleteTask }: TaskSimulatorProps) {
  const [editingTaskId, setEditingTaskId] = useState<string>();
  const [draft, setDraft] = useState<TaskEditorDraft>(makeEmptyDraft);

  useEffect(() => {
    setEditingTaskId(undefined);
    setDraft(makeEmptyDraft());
  }, [detail?.summary.workflowId]);

  if (detail === undefined) {
    return null;
  }

  const editingTask = detail.tasks.find((task) => task.id === editingTaskId);

  return (
    <section className="panel simulator-panel">
      <div className="panel-header">
        <div>
          <p className="eyebrow">Simulation</p>
          <h2>Orders and task schedules</h2>
        </div>
        <div className="toolbar-actions">
          <span className="pill">{detail.tasks.length} tasks</span>
          <button
            className="action-button"
            disabled={busy}
            onClick={() => {
              setEditingTaskId(undefined);
              setDraft(makeEmptyDraft());
            }}
            type="button"
          >
            New order
          </button>
        </div>
      </div>

      <div className="task-list">
        {detail.tasks.length === 0 ? (
          <p className="muted">No tasks are currently scheduled for this workflow.</p>
        ) : (
          detail.tasks.map((task) => (
            <div className="task-row" key={task.id}>
              <div>
                <strong>{task.id}</strong>
                <p>
                  {task.requestedAt} • {task.durationMinutes} min • priority {task.priority}
                </p>
              </div>
              <div className="task-row-actions">
                <button
                  className="action-button action-button--quiet"
                  disabled={busy}
                  onClick={() => {
                    setEditingTaskId(task.id);
                    setDraft(makeDraftFromTask(task));
                  }}
                  type="button"
                >
                  Edit
                </button>
                <button
                  className="action-button action-button--danger"
                  disabled={busy}
                  onClick={() => {
                    void onDeleteTask({ workflowId: detail.summary.workflowId, taskId: task.id });
                  }}
                  type="button"
                >
                  Delete
                </button>
              </div>
            </div>
          ))
        )}
      </div>

      <form
        className="task-form"
        onSubmit={(event) => {
          event.preventDefault();
          const task = mergeTaskDraft(editingTask, draft);
          void onSaveTask({
            workflowId: detail.summary.workflowId,
            task,
            note: draft.note.trim().length > 0 ? draft.note.trim() : undefined,
          }).then(() => {
            setEditingTaskId(undefined);
            setDraft(makeEmptyDraft());
          });
        }}
      >
        <div className="form-grid">
          <label className="field">
            <span>Task ID</span>
            <input
              disabled={busy}
              onChange={(event) => {
                setDraft((current) => ({ ...current, id: event.target.value }));
              }}
              placeholder="pick-201"
              required
              value={draft.id}
            />
          </label>

          <label className="field">
            <span>Requested start</span>
            <input
              disabled={busy}
              onChange={(event) => {
                setDraft((current) => ({ ...current, requestedAt: event.target.value }));
              }}
              required
              type="datetime-local"
              value={draft.requestedAt}
            />
          </label>

          <label className="field">
            <span>Deadline</span>
            <input
              disabled={busy}
              onChange={(event) => {
                setDraft((current) => ({ ...current, deadlineAt: event.target.value }));
              }}
              type="datetime-local"
              value={draft.deadlineAt}
            />
          </label>

          <label className="field">
            <span>Duration (minutes)</span>
            <input
              disabled={busy}
              min={1}
              onChange={(event) => {
                setDraft((current) => ({ ...current, durationMinutes: Number(event.target.value) || 1 }));
              }}
              type="number"
              value={draft.durationMinutes}
            />
          </label>

          <label className="field">
            <span>Priority</span>
            <input
              disabled={busy}
              min={0}
              onChange={(event) => {
                setDraft((current) => ({ ...current, priority: Number(event.target.value) || 0 }));
              }}
              type="number"
              value={draft.priority}
            />
          </label>

          <label className="field">
            <span>Preferred actor</span>
            <input
              disabled={busy}
              list="actor-suggestions"
              onChange={(event) => {
                setDraft((current) => ({ ...current, preferredActorId: event.target.value }));
              }}
              placeholder="robot_4"
              value={draft.preferredActorId}
            />
            <datalist id="actor-suggestions">
              {detail.actors.map((actor) => (
                <option key={actor.id} value={actor.id} />
              ))}
            </datalist>
          </label>
        </div>

        <div className="checkbox-row">
          <label className="checkbox-field">
            <input
              checked={draft.mandatory}
              disabled={busy}
              onChange={(event) => {
                setDraft((current) => ({ ...current, mandatory: event.target.checked }));
              }}
              type="checkbox"
            />
            Mandatory
          </label>

          <label className="checkbox-field">
            <input
              checked={draft.preemptible}
              disabled={busy}
              onChange={(event) => {
                setDraft((current) => ({ ...current, preemptible: event.target.checked }));
              }}
              type="checkbox"
            />
            Preemptible
          </label>
        </div>

        <label className="field">
          <span>Change note</span>
          <textarea
            disabled={busy}
            onChange={(event) => {
              setDraft((current) => ({ ...current, note: event.target.value }));
            }}
            placeholder="Explain why this order changed so the next operator understands the context."
            rows={3}
            value={draft.note}
          />
        </label>

        <div className="toolbar-actions">
          <button
            className="action-button action-button--primary"
            disabled={busy || draft.id.trim().length === 0 || draft.requestedAt.length === 0}
            type="submit"
          >
            {editingTask ? "Update order" : "Insert order"}
          </button>
          <button
            className="action-button action-button--quiet"
            disabled={busy}
            onClick={() => {
              setEditingTaskId(undefined);
              setDraft(makeEmptyDraft());
            }}
            type="button"
          >
            Clear form
          </button>
        </div>
      </form>
    </section>
  );
}

import { useEffect, useState } from "react";
import {
  fromDatetimeLocalValue,
  formatDateTime,
  minutesToMilliseconds,
  toDatetimeLocalValue,
} from "../lib/date-time";
import type {
  DeleteTaskCommand,
  TaskEditorDraft,
  WorkflowDetail,
  WorkflowScheduleMode,
  WorkflowTask,
  WorkflowTaskMutation,
} from "../lib/types";

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

function mergeTaskDraft(
  existingTask: WorkflowTask | undefined,
  draft: TaskEditorDraft,
  requiredCapabilities: string[],
  dependencyTaskIds: string[],
  defaultScheduleMode: WorkflowScheduleMode | undefined,
  defaultScheduleAnchorMs: number | undefined,
): WorkflowTask {
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
    scheduleMode: existingTask?.scheduleMode ?? defaultScheduleMode,
    scheduleAnchorMs: existingTask?.scheduleAnchorMs ?? defaultScheduleAnchorMs,
    priority: draft.priority,
    demand: existingTask?.demand,
    mandatory: draft.mandatory,
    preemptible: draft.preemptible,
    allowedActorTypes: existingTask?.allowedActorTypes ?? [],
    allowedActorIds: existingTask?.allowedActorIds ?? [],
    preferredActorIds: draft.preferredActorId.length > 0 ? [draft.preferredActorId] : [],
    requiredCapabilities,
    dependencyTaskIds,
    mutuallyExclusiveTaskIds: existingTask?.mutuallyExclusiveTaskIds ?? [],
    actorDistances: existingTask?.actorDistances ?? [],
    tardinessCostPerUnit: existingTask?.tardinessCostPerUnit ?? 0,
    earlyStartBonus: existingTask?.earlyStartBonus ?? 0,
    phaseDurations: existingTask?.phaseDurations ?? [],
  };
}

function collectCapabilities(detail: WorkflowDetail): string[] {
  const capabilitySet = new Set<string>();
  for (const actor of detail.actors) {
    for (const capability of actor.capabilities) {
      capabilitySet.add(capability);
    }
  }
  return [...capabilitySet].sort();
}

function validateDeadlineAfterStart(requestedAt: string, deadlineAt: string): string | undefined {
  if (requestedAt.length === 0 || deadlineAt.length === 0) {
    return undefined;
  }
  const requestedMs = fromDatetimeLocalValue(requestedAt);
  const deadlineMs = fromDatetimeLocalValue(deadlineAt);
  if (deadlineMs > 0 && requestedMs > 0 && deadlineMs <= requestedMs) {
    return "Deadline must be after the requested start time.";
  }
  return undefined;
}

export function TaskSimulator({ detail, busy, onSaveTask, onDeleteTask }: TaskSimulatorProps) {
  const [editingTaskId, setEditingTaskId] = useState<string>();
  const [draft, setDraft] = useState<TaskEditorDraft>(makeEmptyDraft);
  const [requiredCapabilities, setRequiredCapabilities] = useState<string[]>([]);
  const [dependencyTaskIds, setDependencyTaskIds] = useState<string[]>([]);

  useEffect(() => {
    setEditingTaskId(undefined);
    setDraft(makeEmptyDraft());
    setRequiredCapabilities([]);
    setDependencyTaskIds([]);
  }, [detail?.summary.workflowId]);

  if (detail === undefined) {
    return null;
  }

  const editingTask = detail.tasks.find((task) => task.id === editingTaskId);
  const availableCapabilities = collectCapabilities(detail);
  const deadlineError = validateDeadlineAfterStart(draft.requestedAt, draft.deadlineAt);

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
              setRequiredCapabilities([]);
              setDependencyTaskIds([]);
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
                  {task.requiredCapabilities.length > 0 ? ` • requires ${task.requiredCapabilities.join(", ")}` : ""}
                </p>
              </div>
              <div className="task-row-actions">
                <button
                  className="action-button action-button--quiet"
                  disabled={busy}
                  onClick={() => {
                    setEditingTaskId(task.id);
                    setDraft(makeDraftFromTask(task));
                    setRequiredCapabilities(task.requiredCapabilities);
                    setDependencyTaskIds(task.dependencyTaskIds);
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
          if (deadlineError) {
            return;
          }
          const task = mergeTaskDraft(
            editingTask,
            draft,
            requiredCapabilities,
            dependencyTaskIds,
            detail.scheduleMode,
            detail.scheduleAnchorMs,
          );
          void onSaveTask({
            workflowId: detail.summary.workflowId,
            task,
            note: draft.note.trim().length > 0 ? draft.note.trim() : undefined,
          }).then(() => {
            setEditingTaskId(undefined);
            setDraft(makeEmptyDraft());
            setRequiredCapabilities([]);
            setDependencyTaskIds([]);
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

          <div className="field">
            <label>
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
            {deadlineError ? <p className="field-error">{deadlineError}</p> : null}
          </div>

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

          <div className="field">
            <label>
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
            <p className="field-hint">Higher values are scheduled first.</p>
          </div>

          <label className="field">
            <span>Preferred actor</span>
            <select
              disabled={busy}
              onChange={(event) => {
                setDraft((current) => ({ ...current, preferredActorId: event.target.value }));
              }}
              value={draft.preferredActorId}
            >
              <option value="">No preference</option>
              {detail.actors.map((actor) => (
                <option key={actor.id} value={actor.id}>
                  {actor.id} ({actor.type}{actor.capabilities.length > 0 ? `: ${actor.capabilities.join(", ")}` : ""})
                </option>
              ))}
            </select>
          </label>
        </div>

        {availableCapabilities.length > 0 ? (
          <fieldset className="fieldset">
            <legend>Required capabilities</legend>
            <span className="field-hint">The assigned actor must have all selected capabilities.</span>
            <div className="checkbox-row">
              {availableCapabilities.map((capability) => (
                <label className="checkbox-field" key={capability}>
                  <input
                    checked={requiredCapabilities.includes(capability)}
                    disabled={busy}
                    onChange={(event) => {
                      if (event.target.checked) {
                        setRequiredCapabilities((current) => [...current, capability]);
                      } else {
                        setRequiredCapabilities((current) => current.filter((c) => c !== capability));
                      }
                    }}
                    type="checkbox"
                  />
                  {capability}
                </label>
              ))}
            </div>
          </fieldset>
        ) : null}

        {detail.tasks.length > 0 ? (
          <fieldset className="fieldset">
            <legend>Dependencies</legend>
            <span className="field-hint">This task will not start until all selected tasks have finished.</span>
            <div className="checkbox-row">
              {detail.tasks
                .filter((task) => task.id !== draft.id)
                .map((task) => (
                  <label className="checkbox-field" key={task.id}>
                    <input
                      checked={dependencyTaskIds.includes(task.id)}
                      disabled={busy}
                      onChange={(event) => {
                        if (event.target.checked) {
                          setDependencyTaskIds((current) => [...current, task.id]);
                        } else {
                          setDependencyTaskIds((current) => current.filter((id) => id !== task.id));
                        }
                      }}
                      type="checkbox"
                    />
                    {task.id}
                  </label>
                ))}
            </div>
          </fieldset>
        ) : null}

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
            rows={2}
            value={draft.note}
          />
        </label>

        <div className="toolbar-actions">
          <button
            className="action-button action-button--primary"
            disabled={busy || draft.id.trim().length === 0 || draft.requestedAt.length === 0 || deadlineError !== undefined}
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
              setRequiredCapabilities([]);
              setDependencyTaskIds([]);
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

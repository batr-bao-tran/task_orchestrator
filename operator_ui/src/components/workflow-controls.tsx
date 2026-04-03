import { useState } from "react";
import { fromDatetimeLocalValue } from "../lib/date-time";
import type {
  ActorStateOverride,
  ManualInterventionCommand,
  TaskStateOverride,
  WorkflowActionCommand,
  WorkflowDetail,
} from "../lib/types";

interface WorkflowControlsProps {
  detail?: WorkflowDetail;
  busy: boolean;
  onPause: (command: WorkflowActionCommand) => Promise<void>;
  onResume: (command: WorkflowActionCommand) => Promise<void>;
  onCancel: (command: WorkflowActionCommand) => Promise<void>;
  onManualIntervention: (command: ManualInterventionCommand) => Promise<void>;
}

interface TaskOverrideDraft {
  taskId: string;
  completed: boolean;
  priority: string;
  deadlineAt: string;
  pinnedActorId: string;
}

interface ActorOverrideDraft {
  actorId: string;
  unavailable: boolean;
  capacity: string;
}

function makeEmptyTaskOverride(): TaskOverrideDraft {
  return { taskId: "", completed: false, priority: "", deadlineAt: "", pinnedActorId: "" };
}

function makeEmptyActorOverride(): ActorOverrideDraft {
  return { actorId: "", unavailable: false, capacity: "" };
}

function buildTaskOverrides(drafts: TaskOverrideDraft[]): TaskStateOverride[] {
  return drafts
    .filter((draft) => draft.taskId.length > 0)
    .map((draft) => {
      const override: TaskStateOverride = { taskId: draft.taskId };
      if (draft.completed) {
        override.completed = true;
      }
      if (draft.priority.length > 0) {
        override.priority = Number(draft.priority);
      }
      if (draft.deadlineAt.length > 0) {
        override.deadline = fromDatetimeLocalValue(draft.deadlineAt);
      }
      if (draft.pinnedActorId.length > 0) {
        override.pinnedActorId = draft.pinnedActorId;
      }
      return override;
    });
}

function buildActorOverrides(drafts: ActorOverrideDraft[]): ActorStateOverride[] {
  return drafts
    .filter((draft) => draft.actorId.length > 0)
    .map((draft) => {
      const override: ActorStateOverride = { actorId: draft.actorId };
      if (draft.unavailable) {
        override.unavailable = true;
      }
      if (draft.capacity.length > 0) {
        override.capacity = Number(draft.capacity);
      }
      return override;
    });
}

export function WorkflowControls({
  detail,
  busy,
  onPause,
  onResume,
  onCancel,
  onManualIntervention,
}: WorkflowControlsProps) {
  const [note, setNote] = useState("");
  const [triggerReorchestration, setTriggerReorchestration] = useState(true);
  const [taskOverrides, setTaskOverrides] = useState<TaskOverrideDraft[]>([]);
  const [actorOverrides, setActorOverrides] = useState<ActorOverrideDraft[]>([]);

  if (detail === undefined) {
    return null;
  }

  const command = {
    workflowId: detail.summary.workflowId,
  };

  const hasOverrides = taskOverrides.some((t) => t.taskId.length > 0) || actorOverrides.some((a) => a.actorId.length > 0);
  const hasNote = note.trim().length > 0;

  return (
    <section className="panel controls-panel">
      <div className="panel-header">
        <div>
          <p className="eyebrow">Lifecycle</p>
          <h2>Operator controls</h2>
        </div>
        <span className="pill">{busy ? "updating" : "ready"}</span>
      </div>

      <div className="control-actions">
        {detail.summary.state === "paused" ? (
          <button
            className="action-button"
            disabled={busy}
            onClick={() => {
              void onResume({ ...command, reason: "Workflow resumed by an operator." });
            }}
            type="button"
          >
            Resume workflow
          </button>
        ) : (
          <button
            className="action-button"
            disabled={busy || detail.summary.state === "cancelled" || detail.summary.state === "failed"}
            onClick={() => {
              void onPause({ ...command, reason: "Workflow paused by an operator." });
            }}
            type="button"
          >
            Pause workflow
          </button>
        )}

        <button
          className="action-button action-button--danger"
          disabled={busy || detail.summary.state === "cancelled"}
          onClick={() => {
            void onCancel({ ...command, reason: "Workflow cancelled by an operator." });
          }}
          type="button"
        >
          Cancel workflow
        </button>
      </div>

      <div className="subpanel">
        <h3>Task overrides</h3>
        <span className="field-hint">
          Mark tasks completed, change priority or deadline, or pin a task to a specific actor for the next plan.
        </span>
        {taskOverrides.map((draft, index) => (
          <div className="override-row" key={index}>
            <div className="form-grid">
              <label className="field">
                <span>Task</span>
                <select
                  disabled={busy}
                  onChange={(event) => {
                    setTaskOverrides((current) =>
                      current.map((item, i) => (i === index ? { ...item, taskId: event.target.value } : item)),
                    );
                  }}
                  value={draft.taskId}
                >
                  <option value="">Select a task</option>
                  {detail.tasks.map((task) => (
                    <option key={task.id} value={task.id}>
                      {task.id} (priority {task.priority})
                    </option>
                  ))}
                </select>
              </label>
              <label className="field">
                <span>New priority</span>
                <input
                  disabled={busy}
                  min={0}
                  onChange={(event) => {
                    setTaskOverrides((current) =>
                      current.map((item, i) => (i === index ? { ...item, priority: event.target.value } : item)),
                    );
                  }}
                  placeholder="Leave empty to keep current"
                  type="number"
                  value={draft.priority}
                />
              </label>
              <label className="field">
                <span>New deadline</span>
                <input
                  disabled={busy}
                  onChange={(event) => {
                    setTaskOverrides((current) =>
                      current.map((item, i) => (i === index ? { ...item, deadlineAt: event.target.value } : item)),
                    );
                  }}
                  type="datetime-local"
                  value={draft.deadlineAt}
                />
              </label>
              <label className="field">
                <span>Pin to actor</span>
                <select
                  disabled={busy}
                  onChange={(event) => {
                    setTaskOverrides((current) =>
                      current.map((item, i) => (i === index ? { ...item, pinnedActorId: event.target.value } : item)),
                    );
                  }}
                  value={draft.pinnedActorId}
                >
                  <option value="">No pin</option>
                  {detail.actors.map((actor) => (
                    <option key={actor.id} value={actor.id}>
                      {actor.id} ({actor.type})
                    </option>
                  ))}
                </select>
              </label>
            </div>
            <div className="checkbox-row">
              <label className="checkbox-field">
                <input
                  checked={draft.completed}
                  disabled={busy}
                  onChange={(event) => {
                    setTaskOverrides((current) =>
                      current.map((item, i) => (i === index ? { ...item, completed: event.target.checked } : item)),
                    );
                  }}
                  type="checkbox"
                />
                Mark completed
              </label>
              <button
                className="action-button action-button--quiet"
                disabled={busy}
                onClick={() => {
                  setTaskOverrides((current) => current.filter((_, i) => i !== index));
                }}
                type="button"
              >
                Remove
              </button>
            </div>
          </div>
        ))}
        <button
          className="action-button"
          disabled={busy || detail.tasks.length === 0}
          onClick={() => {
            setTaskOverrides((current) => [...current, makeEmptyTaskOverride()]);
          }}
          type="button"
        >
          Add task override
        </button>
      </div>

      <div className="subpanel">
        <h3>Actor overrides</h3>
        <span className="field-hint">
          Mark actors unavailable or adjust their capacity for the next planning pass.
        </span>
        {actorOverrides.map((draft, index) => (
          <div className="override-row" key={index}>
            <div className="form-grid">
              <label className="field">
                <span>Actor</span>
                <select
                  disabled={busy}
                  onChange={(event) => {
                    setActorOverrides((current) =>
                      current.map((item, i) => (i === index ? { ...item, actorId: event.target.value } : item)),
                    );
                  }}
                  value={draft.actorId}
                >
                  <option value="">Select an actor</option>
                  {detail.actors.map((actor) => (
                    <option key={actor.id} value={actor.id}>
                      {actor.id} ({actor.type}
                      {actor.capacity !== undefined ? `, capacity ${String(actor.capacity)}` : ""})
                    </option>
                  ))}
                </select>
              </label>
              <label className="field">
                <span>New capacity</span>
                <input
                  disabled={busy}
                  min={0}
                  onChange={(event) => {
                    setActorOverrides((current) =>
                      current.map((item, i) => (i === index ? { ...item, capacity: event.target.value } : item)),
                    );
                  }}
                  placeholder="Leave empty to keep current"
                  type="number"
                  value={draft.capacity}
                />
              </label>
            </div>
            <div className="checkbox-row">
              <label className="checkbox-field">
                <input
                  checked={draft.unavailable}
                  disabled={busy}
                  onChange={(event) => {
                    setActorOverrides((current) =>
                      current.map((item, i) => (i === index ? { ...item, unavailable: event.target.checked } : item)),
                    );
                  }}
                  type="checkbox"
                />
                Mark unavailable
              </label>
              <button
                className="action-button action-button--quiet"
                disabled={busy}
                onClick={() => {
                  setActorOverrides((current) => current.filter((_, i) => i !== index));
                }}
                type="button"
              >
                Remove
              </button>
            </div>
          </div>
        ))}
        <button
          className="action-button"
          disabled={busy || detail.actors.length === 0}
          onClick={() => {
            setActorOverrides((current) => [...current, makeEmptyActorOverride()]);
          }}
          type="button"
        >
          Add actor override
        </button>
      </div>

      <label className="field">
        <span>Operator note</span>
        <textarea
          disabled={busy}
          onChange={(event) => {
            setNote(event.target.value);
          }}
          placeholder="Describe why you are making this change, e.g. 'Dock 2 congested, pulling robot_2 offline until cleared'."
          rows={3}
          value={note}
        />
      </label>

      <label className="checkbox-field">
        <input
          checked={triggerReorchestration}
          disabled={busy}
          onChange={(event) => {
            setTriggerReorchestration(event.target.checked);
          }}
          type="checkbox"
        />
        Trigger replanning after recording the note
      </label>

      <button
        className="action-button action-button--primary"
        disabled={busy || (!hasNote && !hasOverrides)}
        onClick={() => {
          const builtTaskOverrides = buildTaskOverrides(taskOverrides);
          const builtActorOverrides = buildActorOverrides(actorOverrides);
          void onManualIntervention({
            workflowId: detail.summary.workflowId,
            note: note.trim().length > 0 ? note.trim() : "Manual intervention applied.",
            taskOverrides: builtTaskOverrides.length > 0 ? builtTaskOverrides : undefined,
            actorOverrides: builtActorOverrides.length > 0 ? builtActorOverrides : undefined,
            triggerReorchestration,
          }).then(() => {
            setNote("");
            setTriggerReorchestration(true);
            setTaskOverrides([]);
            setActorOverrides([]);
          });
        }}
        type="button"
      >
        Record intervention
      </button>
    </section>
  );
}

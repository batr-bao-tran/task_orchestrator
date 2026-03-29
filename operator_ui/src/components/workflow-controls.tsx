import { useState } from "react";
import type { ManualInterventionCommand, WorkflowActionCommand, WorkflowDetail } from "../lib/types";

interface WorkflowControlsProps {
  detail?: WorkflowDetail;
  busy: boolean;
  onPause: (command: WorkflowActionCommand) => Promise<void>;
  onResume: (command: WorkflowActionCommand) => Promise<void>;
  onCancel: (command: WorkflowActionCommand) => Promise<void>;
  onManualIntervention: (command: ManualInterventionCommand) => Promise<void>;
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

  if (detail === undefined) {
    return null;
  }

  const command = {
    workflowId: detail.summary.workflowId,
  };

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

      <label className="field">
        <span>Operator note</span>
        <textarea
          disabled={busy}
          onChange={(event) => {
            setNote(event.target.value);
          }}
          placeholder="Describe the live condition you want the planner and the next operator to understand."
          rows={4}
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
        disabled={busy || note.trim().length === 0}
        onClick={() => {
          void onManualIntervention({
            workflowId: detail.summary.workflowId,
            note: note.trim(),
            triggerReorchestration,
          }).then(() => {
            setNote("");
            setTriggerReorchestration(true);
          });
        }}
        type="button"
      >
        Record intervention
      </button>
    </section>
  );
}

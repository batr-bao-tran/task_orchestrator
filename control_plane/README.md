# Control Plane

This package sits above the runtime API and application launcher.

Responsibilities:

- durable workflow records
- append-only runtime event history
- plan version snapshots and diffs
- audit trail and idempotency records
- lifecycle queries and operator actions
- integration binding registry for inbound and outbound adapters

Package layout:

- `store/`: persistence abstractions and the SQLite WAL-backed durable store
- `service/`: the control-plane wrapper, event-storage management, and plan-diff utilities
- `integration/`: adapter and binding abstractions for webhooks, schedules, queues, and outbound callbacks

Design intent:

The `ControlPlanService` wraps an existing `protocol::WorkflowRuntimeService`. That keeps the current runtime API working while persisting events, plans, and workflow metadata for operator history and recovery workflows (lifecycle management).

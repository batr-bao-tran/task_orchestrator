# Operator UI

`operator_ui` is the operator-facing web app for the Task Orchestrator control plane.

Its job is to make the workflow system understandable and controllable for a human operator. The app gives an operations team one place to:

- What workflows are currently active, paused, failed, or recovering?
- What changed between plan versions?
- What happened recently in the workflow event timeline?
- Which integrations are currently configured and online?
- Insert, reschedule, or delete orders and task schedules to simulate a live workflow.

## What The App Shows

At a high level, the app is split into three operator views:

- Durable workflow list: a quick status board of tracked workflows, their lifecycle state, latest plan version, recent activity, and last visible error.
- Workflow detail: the selected workflow's latest summary, operator note, assignment diff, event timeline, and audit history.
- Workflow controls: pause, resume, cancel, and manual intervention actions for the selected workflow.
- Orders and task schedules: an operator form for inserting, editing, and deleting tasks with date/time fields so live scenarios can be simulated quickly.
- Integration panel: a simple overview of inbound and outbound bindings such as webhooks, schedules, callbacks, and queue consumers.

This is meant to become the main control-room view for the new control-plane layer that sits above the runtime planner.

## Live And Mock Modes

The app now supports two data modes:

- `live`: calls the C++ operator API over HTTP.
- `mock`: uses local in-memory fixtures for design review, demos, and frontend-only work.
- `auto`: tries the live API first and falls back to mock mode if the backend is unavailable.

Mock mode is backed by `src/lib/mock-data.ts` and the stateful mock client in `src/lib/operator-client.ts`.

## Run Locally

From the repo root:

```bash
cd operator_ui
npm install
npm run dev
```

By default the Vite dev server proxies `/v1/...` requests to `http://127.0.0.1:8080`.

If your backend is listening elsewhere, set:

```bash
export VITE_OPERATOR_API_BASE_URL=http://127.0.0.1:9090
```

You can also force the data mode:

```bash
export VITE_OPERATOR_DATA_MODE=live
export VITE_OPERATOR_DATA_MODE=mock
export VITE_OPERATOR_DATA_MODE=auto
```

Vite will print the local development URL, usually:

```text
http://localhost:5173
```

## Run With The C++ Backend

1. Start the Task Orchestrator server with the control plane enabled.
2. Ensure the HTTP server is reachable from the browser, commonly on `http://127.0.0.1:8080`.
3. Start the UI with `VITE_OPERATOR_DATA_MODE=live npm run dev`.
4. Open the browser app and confirm the mode badge shows `live mode`.

The UI uses one aggregated dashboard request for the main operator screen so it does not need to fan out across multiple browser calls on each refresh.

## Build For Production

```bash
cd operator_ui
npm install
npm run build
```

To preview the production build locally:

```bash
npm run preview
```

## Quality Checks

The package now includes linting, unit tests, and a coverage gate:

```bash
cd operator_ui
npm run lint
npm run test
npm run test:coverage
```

For a single local quality pass that mirrors the package expectations:

```bash
npm run check
```

## How To Use The App

1. Start the dev server.
2. Open the app in your browser.
3. Check the mode badge to confirm whether you are using live or mock data.
4. Click a workflow in the left-hand workflow list.
5. Review the selected workflow's current state, latest plan version, operator note, and recent operator actions.
6. Inspect the assignment diff to understand what changed between plan versions.
7. Read the event timeline to see the recent durable history.
8. Use the workflow controls to pause, resume, cancel, or record manual interventions.
9. Use the task simulator form to insert a new order, reschedule an existing task, or delete a task entirely.
10. Check the integrations panel to understand which connectors are enabled.

## Main Features

- Workflow lifecycle visibility for planned, paused, failed, and recovering workflows
- Plan-diff view for assignment changes across versions
- Event timeline for recent durable workflow history
- Operator audit history for manual interventions and handoffs
- Real-time order and task schedule simulation with date/time inputs
- Integration status panel for inbound and outbound connector bindings
- Live backend mode with explicit mock fallback for demos and local development

## Development Notes

- App entry point: `src/app.tsx`
- Dashboard data hook: `src/hooks/use-operator-dashboard.ts`
- HTTP and mock client layer: `src/lib/operator-client.ts`
- Workflow list component: `src/components/workflow-table.tsx`
- Workflow detail component: `src/components/workflow-detail.tsx`
- Workflow controls: `src/components/workflow-controls.tsx`
- Task simulator: `src/components/task-simulator.tsx`
- Integration panel: `src/components/integration-panel.tsx`
- Mock fixtures: `src/lib/mock-data.ts`

The live browser contract is defined in:

- `../protocol/proto/operator_api.proto`
- `../protocol/proto/operator_api_service.proto`

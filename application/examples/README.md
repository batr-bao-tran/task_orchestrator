# Application Examples

## Warehouse Continuous Pipeline

Use these files for a live warehouse demo:

- `application/examples/application_configs/warehouse_continuous_pipeline.yaml`
- `application/examples/workflow_configs/warehouse_continuous_pipeline.yaml`

The workflow models:

- 4 robots, with `robot_1` and `robot_2` handling inbound and outbound transfers
- 3 humans that can pick and pack
- 2 inbound offload machines, 1 outbound loading machine, and 1 forklift
- 4 vans with dock windows that represent when each vehicle is present
- a constrained outbound wave where three urgent orders compete for one loader, so at least one order should remain unfulfilled

The current workflow schema reserves one actor per task, so van presence is represented by van availability windows plus the requested/deadline windows on load and offload tasks.

Run the backend from the repo root:

```bash
rm -f .task-orchestrator/control-plane/warehouse_continuous_pipeline.sqlite3*
bazel run //application:run_config -- "$PWD/application/examples/application_configs/warehouse_continuous_pipeline.yaml"
```

Run the operator UI in another terminal:

```bash
cd operator_ui
npm install
VITE_OPERATOR_DATA_MODE=live npm run dev
```

Open `http://localhost:5173`, select `warehouse_continuous_pipeline`, and use the UI to keep work flowing:

1. Use **Create workflow simulation** to clone the template into another shift or wave.
2. Use **Orders and task schedules** to add rush tasks or tighten deadlines.
3. Use **Actor overrides** to mark `forklift_1`, `robot_1`, `robot_2`, or `outbound_load_machine_1` unavailable and trigger reorchestration.

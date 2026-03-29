# Task Orchestrator

Task Orchestrator is a C++20 workflow planning and runtime orchestration system for constrained work across multiple actors. It combines deterministic workflow ingestion, global optimization, low-latency orchestration, and runtime re-planning behind one repo.

The repo has two main entry points:

- `//task_orchestrator:task_orchestrator` for the reusable planning and orchestration library
- `//application:run_config` for the executable that can run one-shot jobs or host long-running CLI, HTTP, and gRPC interfaces

More architectural context lives in [DESIGN.md](DESIGN.md).

## What it supports

- Structured YAML workflow input for stable integrations
- Controlled natural-language workflow input for deterministic text-based requests
- Runtime workflow submission and re-orchestration
- Actor and task overrides during runtime
- Optional HTTP and gRPC transports with shared auth and TLS configuration
- Three optimizer backends:
  - `indexed_exact` in the default build
  - `ortools_cp_sat` in the optional-backend build
  - `commercial_mip` in the optional-backend build

## Requirements

- Bazelisk with the checked-in `.bazelversion`.
- C++20 toolchain
- Linux or macOS for the transport-enabled application targets

## Build

Build the core library:

```bash
bazel build //task_orchestrator:task_orchestrator
```

Build the application binary:

```bash
bazel build //application:run_config
```

Build the application binary with all linked solver backends:

```bash
bazel build //application:run_config_with_optional_backends
```

The default application binary stays self-contained. The optional binary links the external solver adapters as well.

## Run the application

The same executable supports two modes.

### One-shot mode

Use a structured application config that points at a workflow YAML file or a controlled-language text file.

```bash
bazel run //application:run_config -- "$PWD/application/config/examples/one_shot.yaml"
```

The checked-in example looks like this:

```yaml
application:
  mode: one_shot
  request:
    kind: workflow_text
    path: one_shot_request.txt
```

The request file path is resolved relative to the application config file, so the example can live alongside `one_shot_request.txt`.

You can also point `kind: workflow_yaml` at a workflow YAML file when you want the one-shot run to execute structured input instead of controlled text.

### Long-running service mode

Use a structured application config with `mode: serve` to keep the process alive and expose any combination of CLI, HTTP, and gRPC.

Example:

```yaml
application:
  mode: serve
  service:
    security:
      mode: api_key
      expected_credential: local-dev-key
      require_secure_transport: false
    bootstrap_request:
      kind: workflow_yaml
      path: service_bootstrap_workflow.yaml
    interfaces:
      cli:
        enabled: true
        prompt: "planner> "
      http:
        enabled: true
        bind_address: 127.0.0.1
        port: 8080
      grpc:
        enabled: true
        bind_address: 127.0.0.1
        port: 9090
```

Run it with:

```bash
bazel run //application:run_config -- "$PWD/application/config/examples/serve_multi_endpoint.yaml"
```

Behavior:

- enabled HTTP and gRPC servers are started
- the optional bootstrap request is loaded into the in-memory runtime service before serving
- the CLI stays interactive until `quit`, `exit`, or an interrupt signal
- without CLI enabled, the process serves until `SIGINT` or `SIGTERM`

The example configs live in:

- `application/config/examples/one_shot.yaml`
- `application/config/examples/serve_multi_endpoint.yaml`

The CLI supports:

- `help`
- `status`
- `submit-yaml <path>`
- `submit-text <path>`
- `reorchestrate <workflow_id>`
- `quit`

## Runtime API contract

The canonical wire contract is in [task_orchestration.proto](protocol/proto/task_orchestration.proto) and [task_orchestration_service.proto](protocol/proto/task_orchestration_service.proto). They are the source of truth for:

- server-streaming workflow event RPCs
- workflow event types
- expected HTTP / gRPC route mapping

The transport-neutral interfaces live in `protocol/include/protocol/runtime_api.hpp`.

Available transport targets:

- `//protocol:runtime_api`
- `//protocol:http_transport`
- `//protocol:grpc_transport`

Packages that only need the contract can link `//protocol:runtime_api` without pulling in HTTP or gRPC dependencies.

When clients need progress events instead of only the final result, the gRPC API exposes:

- `StreamSubmitWorkflow`
- `StreamReorchestrate`

Those server-streaming RPCs emit `WorkflowEvent` messages and end with a terminal event that carries the final `RuntimeApiResponse`.

## Auth and TLS

Security policy is shared across transports:

- `mode: none`
- `mode: bearer_token`
- `mode: api_key`

TLS is configured through the same transport-agnostic model for both HTTP and gRPC. Certificates and trust roots can come from file paths or inline PEM. The transport layer then maps that configuration into the concrete HTTP or gRPC credential objects.

Typical server-side TLS shape:

```yaml
tls:
  identity:
    certificate_chain:
      file: /etc/task-orchestrator/server-cert.pem
    private_key:
      file: /etc/task-orchestrator/server-key.pem
  client_trust:
    root_certificates:
      file: /etc/task-orchestrator/ca.pem
    use_system_default_roots: false
    verify_peer: true
  require_client_certificate: false
```

## Natural-language input

The text parser is a controlled-language parser, not a freeform prompt interpreter. That keeps the planning path deterministic and testable.

Use YAML for the most stable integrations. If you want a more conversational user experience, place an LLM outside the core and translate freeform input into YAML or the controlled workflow language before submitting it here.

## Testing

Run the test suite:

```bash
bazel test //...
```

Build and run the launcher examples:

```bash
bazel build //application:run_config
bazel run //application:run_config -- "$PWD/application/config/examples/one_shot.yaml"
```

Run repo coverage:

```bash
tools/testing/check_coverage.sh 90
```

## Repository layout

- `task_orchestrator/` contains the core library, optimizer backends, benchmarks, and core tests
- `application/` contains config loading, the runner, the runtime service, and the application binary
- `protocol/` contains the protobuf contract and transport implementations
- `utils/` contains shared infrastructure such as logging, generators, executors, and clocks

## License

This project is licensed under the GNU Affero General Public License v3.0. See [LICENSE](LICENSE).

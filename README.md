# Task Orchestrator

A C++20 library for generic workflow coordination between multiple actors: DAG-based phases and processes, capacity- and availability-aware scheduling, and a Boost.MSM planner state machine for low-latency planning.

To use this project, either build locally with Bazel or use CI/release artifacts. See [Build Project](#build-project) and [Using the library in your project](#using-the-library-in-your-project).

---

## Contents

- [Features](#features)
- [Architecture](#architecture)
- [Requirements](#requirements)
- [Build Project](#build-project)
- [Using the library in your project](#using-the-library-in-your-project)
- [Testing](#testing)
- [How to develop in the repo](#how-to-develop-in-the-repo)
- [License](#license)

---

## Features

- **Workflows**: DAG of phases; each phase has processes and optional sub-processes.
- **Actors**: Resources with a capacity (concurrent tasks) and availability windows (uptime).
- **Scheduler**: Greedy, priority- and capacity-aware assignment (EDF-style) to maximize throughput and uptime.
- **Planner FSM**: Boost.MSM state machine (Idle → Planning → Dispatching → Running) for the planning loop.
- **Orchestrator**: Single facade to set workflow, register actors, start, tick, and complete phases.
- **C++20**: Coroutines (`Generator<Assignment>`), `std::ranges`, and template concepts.
- **Application layer**: Config-driven runner for warehouse-scale workflows; reports unfulfilled tasks and capacity issues.
- **Discrete event simulation**: `SimClock` for discrete-time event scheduling in tests and simulations.
- **lld**: Optional use of the LLVM linker (faster links); CI installs it on Linux.

---

## Architecture

The library is built around an **Orchestrator** facade that holds a **Workflow** (DAG of phases and processes), an **ActorRegistry**, and a **Scheduler**. The scheduler produces **Assignments** (task → actor, start time) respecting capacity and availability constraints; a **PlannerStateMachine** (Boost.MSM) drives the planning loop. The public API lives under `task_orchestrator/include/`.

For component diagrams, data flow, and design rationale, see **[DESIGN.md](DESIGN.md)**.

---

## Requirements

- **Bazel** 6+ (recommended 7.x)
- **C++20** toolchain (GCC 10+, Clang 12+, or MSVC 2019+)
- **Boost** 1.84 (headers only for MSM; fetched by the build when using the provided MODULE.bazel extension)

No build artifacts are stored in the repo; build output is under `bazel-*` and is ignored by Git.

---

## Build Project

Clone the repository:

```bash
git clone <repo-url> task_orchestrator
cd task_orchestrator
```

Build the library:

```bash
bazel build //task_orchestrator:task_orchestrator
```

- **Linux:** If you see `cannot find 'ld'`, install a full C++ toolchain (e.g. `build-essential` on Debian/Ubuntu). For faster linking use the LLVM linker: `sudo apt-get install lld` then `bazel build --config=linux //task_orchestrator:task_orchestrator`.
- **macOS:** Ensure Xcode Command Line Tools are installed (`xcode-select --install`) and selected. Use the default build (no config); do not use `--config=linux` or `--config=gold` (those linkers are Linux-only).

Build the application layer with examples:

```bash
bazel build //application:run_config
```

---

## Using the library in your project

### Option A: Depend on this repo with Bazel

In your `WORKSPACE` or via Bzlmod, add this repository. Then in your `BUILD`:

```python
cc_binary(
    name = "my_app",
    srcs = ["main.cpp"],
    deps = ["@task_orchestrator//:task_orchestrator"],
)
```

Include the public headers, e.g. `#include "task_orchestrator/core/orchestrator.hpp"`.

### Option B: Use a release tarball

From a [GitHub release](.github/workflows/release.yml), download `task_orchestrator-<version>-<platform>.tar.gz` for your platform. Each tarball contains `include/task_orchestrator/` (public headers) and optionally `lib/` (built library). Add `include` to your include path and link against the library. No build artifacts are committed in the repo; tarballs are produced by CI on release.

---

## Testing

Tests use **Google Test** (gtest/gmock). From the repo root:

```bash
bazel test //task_orchestrator/tests/...
bazel test //application/tests/...
# or
bazel test //...
```

Unit tests live in `task_orchestrator/tests/unit/`, scenario tests in `task_orchestrator/tests/scenario/`, and application tests in `application/tests/`.

---

## How to develop in the repo

- **Format and lint**: Install [pre-commit](https://pre-commit.com/) and run `pre-commit install` then `pre-commit run --all-files`. Hooks include Buildifier (BUILD files), clang-format (C++), fix-header-guards, and actionlint. See `.pre-commit-config.yaml`.
- **CI**: On push/PR to `main`/`master`, `.github/workflows/ci.yml` builds the library and runs unit and scenario tests on Ubuntu 22.04 and macOS 14. `.github/workflows/release.yml` produces release tarballs (headers + library) for Linux and macOS.
- **Layout**: `task_orchestrator/` holds public headers in `include/`, implementation in `src/`, benchmarks in `benchmark/`, and tests in `tests/` (unit + scenario). `application/` is the config-driven runner; `third_party/` contains Boost BUILD and Bzlmod. Only source, config, and docs are committed; build output is ignored.
- **More detail**: See [DESIGN.md](DESIGN.md) for architecture. Application config is YAML (`id`, `actors`, `tasks`); run with `bazel run //application:run_config -- <path_to_config>`. Example configs are in `application/examples/`. For discrete-event simulation, see `utils/sim_clock.hpp` and the tests that use it.

---

## License

This project is **open source** under the **GNU Affero General Public License v3.0 (AGPL-3.0)**. See [LICENSE](LICENSE) for the full text.

- You may use, modify, and distribute the software under the terms of the AGPL-3.0.
- **Copyleft**: If you distribute modified versions or provide the software as a network service, you must make the corresponding source code available under the same license. In practice, this means **changes must be shared with the community** — you cannot keep a private fork while distributing or offering the software to others. Contributing changes back upstream is the intended way to comply.

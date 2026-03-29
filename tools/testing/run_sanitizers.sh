#!/usr/bin/env bash

set -euo pipefail

readonly DEFAULT_BAZEL_VERSION="8.5.0"
bazel_version="${USE_BAZEL_VERSION:-$DEFAULT_BAZEL_VERSION}"

bazel_cmd() {
  if [[ -n "${BAZEL_CI_CACHE_DIR:-}" ]] && [[ -x "tools/ci/bazel_ci.sh" ]]; then
    USE_BAZEL_VERSION="${bazel_version}" tools/ci/bazel_ci.sh "$@"
    return
  fi
  USE_BAZEL_VERSION="${bazel_version}" bazel "$@"
}

readonly -a SANITIZER_TEST_TARGETS=(
  "//application/tests:application_test"
  "//application/tests:application_detail_test"
  "//application/tests:config_loader_test"
  "//application/tests:runtime_api_test"
  "//application/runtime_service/tests:in_memory_runtime_service_test"
  "//protocol/tests:grpc_transport_internal_test"
  "//protocol/tests:http_transport_internal_test"
  "//protocol/tests:tls_credentials_test"
  "//utils/tests:generator_test"
  "//utils/tests:task_executor_test"
)

bazel_cmd test \
  --config=linux \
  --config=sanitize \
  "${SANITIZER_TEST_TARGETS[@]}"

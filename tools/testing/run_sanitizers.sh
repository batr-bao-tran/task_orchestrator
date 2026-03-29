#!/usr/bin/env bash

set -euo pipefail

readonly DEFAULT_BAZEL_VERSION="8.5.0"
readonly SANITIZER_BUILD_TARGETS="//..."
readonly SANITIZER_TEST_QUERY="tests(//...)"
bazel_version="${USE_BAZEL_VERSION:-$DEFAULT_BAZEL_VERSION}"

bazel_cmd() {
  if [[ -n "${BAZEL_CI_CACHE_DIR:-}" ]] && [[ -x "tools/ci/bazel_ci.sh" ]]; then
    USE_BAZEL_VERSION="${bazel_version}" tools/ci/bazel_ci.sh "$@"
    return
  fi
  USE_BAZEL_VERSION="${bazel_version}" bazel "$@"
}

mapfile -t sanitizer_test_targets < <(bazel_cmd query "${SANITIZER_TEST_QUERY}")

if [[ "${#sanitizer_test_targets[@]}" -eq 0 ]]; then
  printf 'No Bazel test targets matched %s\n' "${SANITIZER_TEST_QUERY}" >&2
  exit 1
fi

bazel_cmd build \
  --config=linux \
  --config=sanitize \
  "${SANITIZER_BUILD_TARGETS}"

bazel_cmd test \
  --config=linux \
  --config=sanitize \
  "${sanitizer_test_targets[@]}"

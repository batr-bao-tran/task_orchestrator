#!/usr/bin/env bash

set -euo pipefail

if [[ -z "${BAZEL_CI_CACHE_DIR:-}" ]]; then
  exec bazel "$@"
fi

readonly disk_cache_dir="${BAZEL_CI_CACHE_DIR}/disk"
readonly repository_cache_dir="${BAZEL_CI_CACHE_DIR}/repository"

mkdir -p "${disk_cache_dir}" "${repository_cache_dir}"

exec bazel "$@" \
  --disk_cache="${disk_cache_dir}" \
  --repository_cache="${repository_cache_dir}"

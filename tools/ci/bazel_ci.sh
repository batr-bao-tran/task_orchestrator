#!/usr/bin/env bash

set -euo pipefail

disk_cache_dir="${BAZEL_CI_DISK_CACHE_DIR:-}"
repository_cache_dir="${BAZEL_CI_REPOSITORY_CACHE_DIR:-}"

if [[ -z "${disk_cache_dir}" && -n "${BAZEL_CI_CACHE_DIR:-}" ]]; then
  disk_cache_dir="${BAZEL_CI_CACHE_DIR}/disk"
fi

if [[ -z "${repository_cache_dir}" && -n "${BAZEL_CI_CACHE_DIR:-}" ]]; then
  repository_cache_dir="${BAZEL_CI_CACHE_DIR}/repository"
fi

if [[ -z "${disk_cache_dir}" && -z "${repository_cache_dir}" ]]; then
  exec bazel "$@"
fi

declare -a bazel_cache_args=()

if [[ -n "${disk_cache_dir}" ]]; then
  mkdir -p "${disk_cache_dir}"
  bazel_cache_args+=("--disk_cache=${disk_cache_dir}")
fi

if [[ -n "${repository_cache_dir}" ]]; then
  mkdir -p "${repository_cache_dir}"
  bazel_cache_args+=("--repository_cache=${repository_cache_dir}")
fi

exec bazel "$@" "${bazel_cache_args[@]}"

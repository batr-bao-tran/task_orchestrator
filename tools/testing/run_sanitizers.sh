#!/usr/bin/env bash

set -euo pipefail

readonly DEFAULT_BAZEL_VERSION="8.5.0"
readonly SANITIZER_BUILD_TARGETS="//..."
readonly SANITIZER_TEST_QUERY="tests(//...)"
readonly REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bazel_version="${USE_BAZEL_VERSION:-$DEFAULT_BAZEL_VERSION}"

bazel_cmd() {
  if [[ -n "${BAZEL_CI_CACHE_DIR:-}" ]] && [[ -x "tools/ci/bazel_ci.sh" ]]; then
    USE_BAZEL_VERSION="${bazel_version}" tools/ci/bazel_ci.sh "$@"
    return
  fi
  USE_BAZEL_VERSION="${bazel_version}" bazel "$@"
}

read_required_llvm_major() {
  local version_file="${REPO_ROOT}/.llvm-version"
  if [[ ! -f "${version_file}" ]]; then
    printf 'Could not find %s\n' "${version_file}" >&2
    return 1
  fi

  local version
  version="$(tr -d '[:space:]' < "${version_file}")"
  if [[ ! "${version}" =~ ^[0-9]+$ ]]; then
    printf 'Invalid LLVM version in %s\n' "${version_file}" >&2
    return 1
  fi

  printf '%s\n' "${version}"
}

llvm_major_version() {
  local binary="$1"
  local version_output

  if ! version_output="$("${binary}" --version 2>/dev/null)"; then
    return 1
  fi

  sed -n \
    -e 's/.*LLVM version \([0-9][0-9]*\).*/\1/p' \
    -e 's/.*clang version \([0-9][0-9]*\).*/\1/p' \
    <<< "${version_output}" | head -n 1
}

resolve_llvm_binary() {
  local required_major="$1"
  local tool_name="$2"
  local -a candidates=()
  local candidate=""

  if command -v "${tool_name}-${required_major}" >/dev/null 2>&1; then
    candidates+=("$(command -v "${tool_name}-${required_major}")")
  fi
  if command -v "${tool_name}" >/dev/null 2>&1; then
    candidates+=("$(command -v "${tool_name}")")
  fi
  if [[ -x "/opt/llvm-${required_major}/bin/${tool_name}" ]]; then
    candidates+=("/opt/llvm-${required_major}/bin/${tool_name}")
  fi
  if [[ -x "/usr/lib/llvm-${required_major}/bin/${tool_name}" ]]; then
    candidates+=("/usr/lib/llvm-${required_major}/bin/${tool_name}")
  fi

  local -A seen=()
  local major=""
  for candidate in "${candidates[@]}"; do
    if [[ -n "${seen[${candidate}]:-}" ]]; then
      continue
    fi
    seen["${candidate}"]=1
    major="$(llvm_major_version "${candidate}")"
    if [[ "${major}" == "${required_major}" ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done

  printf 'Could not find %s for LLVM %s\n' "${tool_name}" "${required_major}" >&2
  return 1
}

required_llvm_major="$(read_required_llvm_major)"
clang_binary="$(resolve_llvm_binary "${required_llvm_major}" clang)"
clangxx_binary="$(resolve_llvm_binary "${required_llvm_major}" clang++)"
llvm_bin_dir="$(dirname "${clang_binary}")"
export PATH="${llvm_bin_dir}:${PATH}"

sanitizer_toolchain_flags=(
  "--repo_env=CC=${clang_binary}"
  "--repo_env=CXX=${clangxx_binary}"
)

mapfile -t sanitizer_test_targets < <(bazel_cmd query "${SANITIZER_TEST_QUERY}")

if [[ "${#sanitizer_test_targets[@]}" -eq 0 ]]; then
  printf 'No Bazel test targets matched %s\n' "${SANITIZER_TEST_QUERY}" >&2
  exit 1
fi

bazel_cmd build \
  --config=linux \
  --config=sanitize \
  "${sanitizer_toolchain_flags[@]}" \
  "${SANITIZER_BUILD_TARGETS}"

bazel_cmd test \
  --config=linux \
  --config=sanitize \
  "${sanitizer_toolchain_flags[@]}" \
  "${sanitizer_test_targets[@]}"

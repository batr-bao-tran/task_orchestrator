#!/usr/bin/env bash

set -euo pipefail

readonly DEFAULT_THRESHOLD_PERCENT="90"
readonly DEFAULT_BAZEL_VERSION="8.5.0"
readonly COVERAGE_TARGETS="//..."
readonly COVERAGE_FILTER='^//'
readonly BASELINE_REPORT_SUFFIX="/_coverage/_baseline_report.dat"
readonly COVERAGE_REPORT_SUFFIX="/_coverage/_coverage_report.dat"

threshold_percent="${1:-$DEFAULT_THRESHOLD_PERCENT}"
report_path="${2:-}"
bazel_version="${USE_BAZEL_VERSION:-$DEFAULT_BAZEL_VERSION}"

bazel_cmd() {
  if [[ -n "${BAZEL_CI_CACHE_DIR:-}" ]] && [[ -x "tools/ci/bazel_ci.sh" ]]; then
    USE_BAZEL_VERSION="${bazel_version}" tools/ci/bazel_ci.sh "$@"
    return
  fi
  USE_BAZEL_VERSION="${bazel_version}" bazel "$@"
}

if [[ -z "${report_path}" ]]; then
  bazel_cmd coverage "${COVERAGE_TARGETS}" \
    --combined_report=lcov \
    --instrumentation_filter="${COVERAGE_FILTER}"
  report_path="$(bazel_cmd info output_path)${COVERAGE_REPORT_SUFFIX}"
fi

if [[ ! -f "${report_path}" ]]; then
  printf 'Coverage report not found at %s\n' "${report_path}" >&2
  exit 1
fi

if [[ "${report_path}" == *"${BASELINE_REPORT_SUFFIX}" ]]; then
  printf 'Refusing to read baseline coverage report: %s\n' "${report_path}" >&2
  printf 'Use the merged coverage report instead: %s\n' "$(bazel_cmd info output_path)${COVERAGE_REPORT_SUFFIX}" >&2
  exit 1
fi

coverage_summary="$(
  awk '
    /^LF:/ { total_lines += substr($0, 4) }
    /^LH:/ { covered_lines += substr($0, 4) }
    END {
      if (total_lines == 0) {
        printf "0 0 0.00"
        exit 0
      }
      printf "%d %d %.2f", covered_lines, total_lines, (100.0 * covered_lines / total_lines)
    }
  ' "${report_path}"
)"

read -r covered_lines total_lines coverage_percent <<<"${coverage_summary}"

if [[ "${total_lines}" == "0" ]]; then
  printf 'Coverage report contains zero instrumented lines: %s\n' "${report_path}" >&2
  exit 1
fi

printf 'Overall coverage: %s%% (%s/%s lines)\n' \
  "${coverage_percent}" \
  "${covered_lines}" \
  "${total_lines}"

if ! awk -v actual="${coverage_percent}" -v expected="${threshold_percent}" \
  'BEGIN { exit (actual + 0 >= expected + 0) ? 0 : 1 }'; then
  printf 'Coverage threshold not met: expected at least %s%%\n' "${threshold_percent}" >&2
  exit 1
fi

printf 'Coverage threshold met: %s%% >= %s%%\n' \
  "${coverage_percent}" \
  "${threshold_percent}"

#!/usr/bin/env bash

set -euo pipefail

sudo apt-get update
sudo apt-get install -y curl gnupg lsb-release software-properties-common

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT

curl -fsSL https://apt.llvm.org/llvm.sh -o "${tmpdir}/llvm.sh"
chmod +x "${tmpdir}/llvm.sh"
sudo "${tmpdir}/llvm.sh" 22
sudo apt-get install -y clang-22 clang-tidy-22 clang-format-22 lld-22

llvm_bin_dir="/usr/lib/llvm-22/bin"
if [[ -d "${llvm_bin_dir}" ]]; then
  echo "${llvm_bin_dir}" >> "${GITHUB_PATH}"
fi

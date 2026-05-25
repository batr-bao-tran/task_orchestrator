#!/usr/bin/env python3
"""Run clang-format from the pinned LLVM 22 toolchain."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from llvm_tooling import find_llvm_tool, print_error


def main() -> int:
    root = Path.cwd()
    if not (root / "MODULE.bazel").exists() and not (root / "WORKSPACE").exists():
        return print_error("Run from repository root.")

    argv = [arg.strip() for arg in sys.argv[1:] if arg.strip()]
    if not argv:
        return 0

    try:
        return subprocess.run([find_llvm_tool("clang-format"), "-i", *argv], cwd=root, check=False).returncode
    except RuntimeError as error:
        return print_error(str(error))


if __name__ == "__main__":
    sys.exit(main())

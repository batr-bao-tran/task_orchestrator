#!/usr/bin/env python3
"""Minimal shared helpers for pinned LLVM 22 command-line tools."""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

EXPECTED_LLVM_MAJOR = 22


def print_error(message: str) -> int:
    print(message, file=sys.stderr)
    return 1


def find_llvm_tool(tool_basename: str) -> str:
    candidate_names = [
        f"{tool_basename}-{EXPECTED_LLVM_MAJOR}",
        tool_basename,
    ]
    candidate_paths = [
        Path(f"/opt/llvm-{EXPECTED_LLVM_MAJOR}/bin/{tool_basename}"),
        Path(f"/usr/lib/llvm-{EXPECTED_LLVM_MAJOR}/bin/{tool_basename}"),
    ]

    def version_major(tool_path: str) -> int | None:
        try:
            result = subprocess.run(
                [tool_path, "--version"],
                capture_output=True,
                check=True,
                text=True,
            )
        except (OSError, subprocess.CalledProcessError):
            return None
        output = result.stdout or result.stderr
        match = re.search(r"version\s+(\d+)\.", output)
        return int(match.group(1)) if match is not None else None

    for candidate in candidate_names:
        tool_path = shutil.which(candidate)
        if tool_path is not None and version_major(tool_path) == EXPECTED_LLVM_MAJOR:
            return tool_path

    for candidate_path in candidate_paths:
        if candidate_path.exists() and version_major(str(candidate_path)) == EXPECTED_LLVM_MAJOR:
            return str(candidate_path)

    expected = ", ".join(candidate_names + [str(path) for path in candidate_paths])
    raise RuntimeError(
        f"Unable to find {tool_basename} with LLVM major version {EXPECTED_LLVM_MAJOR}. Tried: {expected}."
    )

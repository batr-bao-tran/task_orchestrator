#!/usr/bin/env python3
"""Generate compile_commands.json from Bazel aquery for clang-tidy --fix. Run from repo root."""
from __future__ import annotations

import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    root = Path.cwd()
    if not (root / "WORKSPACE").exists() and not (root / "MODULE.bazel").exists():
        print("Run from repository root.", file=sys.stderr)
        return 1
    r = subprocess.run(
        ["bazel", "aquery", 'mnemonic("CppCompile", //...)', "--output=text", "--include_commandline"],
        cwd=root,
        capture_output=True,
        text=True,
        timeout=120,
    )
    if r.returncode != 0:
        print(r.stderr or r.stdout, file=sys.stderr)
        return r.returncode
    exec_root = root
    output_base = None
    try:
        rc = subprocess.run(["bazel", "info", "execution_root"], cwd=root, capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        rc = None
    if rc is not None and rc.returncode == 0:
        exec_root = Path(rc.stdout.strip())
    try:
        rc = subprocess.run(["bazel", "info", "output_base"], cwd=root, capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        rc = None
    if rc is not None and rc.returncode == 0:
        output_base = Path(rc.stdout.strip())

    def rewrite_include_path(path: str) -> str:
        if output_base is not None and path.startswith("external/"):
            return str((output_base / path).resolve())
        return path

    def normalize_arguments(command: str) -> list[str]:
        args = shlex.split(command)
        rewritten: list[str] = []
        path_flags = {"-I", "-isystem", "-iquote"}
        i = 0
        while i < len(args):
            arg = args[i]
            if arg in path_flags and i + 1 < len(args):
                rewritten.extend([arg, rewrite_include_path(args[i + 1])])
                i += 2
                continue
            for prefix in ("-I", "-isystem", "-iquote"):
                if arg.startswith(prefix) and arg != prefix:
                    rewritten.append(prefix + rewrite_include_path(arg[len(prefix) :]))
                    break
            else:
                rewritten.append(arg)
            i += 1
        return rewritten

    commands = []
    pat = re.compile(r"action '([^']+)'\s*\n(.*?)(?=action '|\Z)", re.DOTALL)
    for m in pat.finditer(r.stdout):
        block = m.group(2)
        cmd_m = re.search(r"Command Line: \(exec ([^)]+(?:\)[^)]*)?)", block, re.DOTALL)
        if not cmd_m:
            continue
        cmd = " ".join(cmd_m.group(1).replace("\\\n", " ").split())
        if " # " in cmd:
            cmd = cmd.split(" # ")[0].rstrip()
        if cmd.endswith(")"):
            cmd = cmd[:-1].rstrip()
        cmd = " ".join(a for a in cmd.split() if a != "-fno-canonical-system-headers")
        src_m = re.search(r"(\S+\.(?:cpp|cc|cxx|c)\b)", m.group(1))
        if not src_m:
            src_m = re.search(r"(-c)\s+(\S+\.(?:cpp|cc|cxx|c)\b)", cmd)
            src = src_m.group(2) if src_m else None
        else:
            src = src_m.group(1)
        if not src:
            continue
        src_str = str(src)
        file_path = (root / src_str).resolve() if not Path(src_str).is_absolute() else Path(src_str)
        path_s = str(file_path)
        if "bazel-out" in path_s or "bazel-bin" in path_s or "/external/" in path_s or "/third_party/" in path_s:
            continue
        commands.append({"directory": str(exec_root), "arguments": normalize_arguments(cmd), "file": path_s})

    out = root / "compile_commands.json"
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=root, prefix="compile_commands.", suffix=".tmp", delete=False) as f:
        json.dump(commands, f, indent=2)
        tmp = Path(f.name)
    os.replace(tmp, out)
    print("Wrote", len(commands), "entries to", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

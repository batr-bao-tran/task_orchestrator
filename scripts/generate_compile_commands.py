#!/usr/bin/env python3
"""Generate compile_commands.json from Bazel aquery for clang-tidy --fix. Run from repo root."""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys
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
    rc = subprocess.run(["bazel", "info", "execution_root"], cwd=root, capture_output=True, text=True, timeout=10)
    if rc.returncode == 0:
        exec_root = Path(rc.stdout.strip())

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
        commands.append({"directory": str(exec_root), "command": cmd, "file": path_s})

    out = root / "compile_commands.json"
    tmp = root / "compile_commands.json.tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(commands, f, indent=2)
    os.replace(tmp, out)
    print("Wrote", len(commands), "entries to", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

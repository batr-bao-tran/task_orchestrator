#!/usr/bin/env python3
"""
Run clang-tidy --fix (generates compile_commands.json from Bazel, then fixes).
External libs live in third_party/ or external/; we only skip those paths.
.clang-tidy excludes build artifacts and third_party/external from diagnostics.
"""
import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    root = Path.cwd()
    if not (root / "MODULE.bazel").exists() and not (root / "WORKSPACE").exists():
        print("Run from repository root.", file=sys.stderr)
        return 1

    argv = [a.strip() for a in sys.argv[1:] if a.strip()]
    if "--fix" in argv:
        argv = [a for a in argv if a != "--fix"]

    gen = root / "scripts" / "generate_compile_commands.py"
    if not gen.exists():
        print("scripts/generate_compile_commands.py not found.", file=sys.stderr)
        return 1
    if subprocess.run([sys.executable, str(gen)], cwd=root).returncode != 0:
        return 1
    db_path = root / "compile_commands.json"
    if not db_path.exists():
        print("compile_commands.json not created.", file=sys.stderr)
        return 1

    def _load_db():
        content = db_path.read_text(encoding="utf-8")
        return json.loads(content)

    def ensure_db():
        try:
            return _load_db()
        except json.JSONDecodeError as e:
            print(f"Invalid JSON in {db_path}: {e}", file=sys.stderr)
            return None

    db = ensure_db()
    if db is None:
        return 1
    suffixes = {".cpp", ".cxx", ".cc", ".c", ".hpp", ".hxx", ".h"}
    # Only skip paths under third_party/ or external/ (external libs separate from our source)
    def skip_external_tu(p: Path) -> bool:
        s = str(p)
        return "/third_party/" in s or "/external/" in s

    if argv:
        files = [
            (root / f).resolve()
            for f in argv
            if Path(f).suffix in suffixes and not skip_external_tu(Path(f))
        ]
    else:
        files = [Path(e["file"]) for e in db if not skip_external_tu(Path(e["file"]))]
    if not files:
        return 0
    return subprocess.run(
        ["clang-tidy", "--fix", "-quiet", "-p", str(root)] + [str(p) for p in files],
        cwd=root,
    ).returncode


if __name__ == "__main__":
    sys.exit(main())

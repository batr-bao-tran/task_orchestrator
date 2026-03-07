#!/usr/bin/env python3
"""
Auto-fix header guards to PROJECT__PATH__FILE_.
"""
from pathlib import Path
import re
import sys

PROJECT_NAME = "TASK_ORCHESTRATOR"


def path_to_guard_part(path: str) -> str:
    """Convert path like task_orchestrator/include/foo/bar.hpp to guard segment (uppercase, / -> _)."""
    return path.replace("/", "_").replace("\\", "_").upper()


def file_to_guard_part(filename: str) -> str:
    """e.g. orchestrator.hpp -> ORCHESTRATOR_HPP_"""
    base = filename.upper().replace(".", "_")
    return f"{base}_" if not base.endswith("_") else base


def compute_guard(relative_path: str) -> str:
    """Full guard: PROJECT__PATH__FILE_ (double underscore between project, path, and file)."""
    p = Path(relative_path)
    path_part = path_to_guard_part(str(p.parent))
    file_part = file_to_guard_part(p.name)
    return f"{PROJECT_NAME}__{path_part}__{file_part}"


def fix_file(filepath: Path, root: Path) -> str | None:
    """Replace first #ifndef/#define with PROJECT__PATH__FILE_. Returns new content or None if no change."""
    text = filepath.read_text(encoding="utf-8", errors="replace")
    rel = str(filepath.relative_to(root))
    new_guard = compute_guard(rel)

    ifndef_pat = re.compile(
        r"#ifndef\s+(\S+)\s*\n#define\s+\S+\s*\n", re.MULTILINE
    )
    match = ifndef_pat.search(text)
    if not match:
        return None
    old_guard = match.group(1)
    if old_guard == new_guard:
        return None

    new_block = f"#ifndef {new_guard}\n#define {new_guard}\n"
    return text[: match.start()] + new_block + text[match.end() :]


def main():
    root = Path(__file__).resolve().parent.parent
    modified = []
    for ext in (".hpp", ".h", ".hxx"):
        for f in root.rglob(f"*{ext}"):
            if "third_party" in str(f) or "bazel-" in str(f) or ".git" in str(f):
                continue
            try:
                new_content = fix_file(f, root)
            except Exception as e:
                print(f"Error {f}: {e}", file=sys.stderr)
                continue
            if new_content is not None:
                f.write_text(new_content, encoding="utf-8")
                modified.append(str(f.relative_to(root)))
    for m in modified:
        print(m)
    return 0


if __name__ == "__main__":
    sys.exit(main())

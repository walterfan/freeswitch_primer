#!/usr/bin/env python3
"""
PKB Staleness Checker — Level 1 automation (zero LLM tokens).

Compares source-code file timestamps against PKB page timestamps to detect
potentially stale documentation. Git commit time is preferred when available,
with filesystem mtime as a fallback.

Usage:
    python check_pkb_staleness.py --repo-root . --doc-dir man
    python check_pkb_staleness.py --json
    python check_pkb_staleness.py --config .pkb-source-doc-map.json

Custom config format:
[
  {"pattern": "src/**/*.ts", "docs": ["01-architecture", "05-workflows"]},
  {"pattern": "package.json", "docs": ["02-tech-stack", "02-build"]}
]
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from glob import glob
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from generated_output import render_json_artifact, render_markdown_artifact, script_id, write_text_file

DEFAULT_SOURCE_DOC_MAP = [
    # Architecture and structure
    {"pattern": "src/**/*.ts", "docs": ["01-architecture", "05-workflows"]},
    {"pattern": "src/**/*.tsx", "docs": ["01-architecture", "05-workflows"]},
    {"pattern": "src/**/*.js", "docs": ["01-architecture", "05-workflows"]},
    {"pattern": "src/**/*.jsx", "docs": ["01-architecture", "05-workflows"]},
    {"pattern": "src/**/*.py", "docs": ["01-architecture", "05-workflows"]},
    {"pattern": "src/**/*.go", "docs": ["01-architecture", "05-workflows"]},
    {"pattern": "src/**/*.rs", "docs": ["01-architecture", "04-data-and-api"]},
    {"pattern": "app/**/*.py", "docs": ["01-architecture", "05-workflows"]},
    {"pattern": "internal/**/*.go", "docs": ["01-architecture", "05-workflows"]},
    {"pattern": "cmd/**/*.go", "docs": ["01-architecture", "03-repo-map"]},
    {"pattern": "server/**/*.ts", "docs": ["01-architecture", "05-workflows"]},
    {"pattern": "services/**/*.py", "docs": ["01-architecture", "05-workflows"]},
    {"pattern": "routes/**/*.ts", "docs": ["04-data-and-api", "05-workflows"]},
    {"pattern": "controllers/**/*.ts", "docs": ["04-data-and-api", "05-workflows"]},
    {"pattern": "handlers/**/*.go", "docs": ["04-data-and-api", "05-workflows"]},
    {"pattern": "migrations/**", "docs": ["04-data-and-api"]},
    {"pattern": "proto/**", "docs": ["04-data-and-api"]},
    {"pattern": "openapi*.yaml", "docs": ["04-data-and-api"]},
    {"pattern": "openapi*.yml", "docs": ["04-data-and-api"]},
    {"pattern": "openapi*.json", "docs": ["04-data-and-api"]},
    # Tech stack and build
    {"pattern": "package.json", "docs": ["02-tech-stack", "02-build"]},
    {"pattern": "pnpm-lock.yaml", "docs": ["02-tech-stack", "02-build"]},
    {"pattern": "package-lock.json", "docs": ["02-tech-stack", "02-build"]},
    {"pattern": "yarn.lock", "docs": ["02-tech-stack", "02-build"]},
    {"pattern": "Cargo.toml", "docs": ["02-tech-stack", "02-build"]},
    {"pattern": "**/Cargo.toml", "docs": ["02-tech-stack", "02-build"]},
    {"pattern": "go.mod", "docs": ["02-tech-stack", "02-build"]},
    {"pattern": "pom.xml", "docs": ["02-tech-stack", "02-build"]},
    {"pattern": "pyproject.toml", "docs": ["02-tech-stack", "02-build", "01-conventions"]},
    {"pattern": "requirements*.txt", "docs": ["02-tech-stack", "02-build"]},
    {"pattern": "vite.config.*", "docs": ["02-build"]},
    {"pattern": "webpack.config.*", "docs": ["02-build"]},
    {"pattern": "Dockerfile*", "docs": ["02-build", "01-runbook"]},
    {"pattern": "docker-compose*.yml", "docs": ["02-build", "01-runbook"]},
    {"pattern": "docker-compose*.yaml", "docs": ["02-build", "01-runbook"]},
    {"pattern": ".github/workflows/*.yml", "docs": ["02-build"]},
    {"pattern": ".github/workflows/*.yaml", "docs": ["02-build"]},
    {"pattern": "Jenkinsfile", "docs": ["02-build"]},
    {"pattern": "Makefile", "docs": ["02-quick-start", "02-build"]},
    # Conventions
    {"pattern": ".editorconfig", "docs": ["01-conventions"]},
    {"pattern": ".eslintrc*", "docs": ["01-conventions"]},
    {"pattern": ".prettierrc*", "docs": ["01-conventions"]},
    {"pattern": "tsconfig*.json", "docs": ["01-conventions"]},
    {"pattern": "rustfmt.toml", "docs": ["01-conventions"]},
    {"pattern": ".flake8", "docs": ["01-conventions"]},
    {"pattern": "mypy.ini", "docs": ["01-conventions"]},
    # Testing
    {"pattern": "tests/**", "docs": ["03-testing"]},
    {"pattern": "test/**", "docs": ["03-testing"]},
    {"pattern": "src/**/*.test.*", "docs": ["03-testing"]},
    {"pattern": "src/**/*_test.*", "docs": ["03-testing"]},
    # Observability
    {"pattern": "monitoring/**", "docs": ["02-observability"]},
    {"pattern": "grafana/**", "docs": ["02-observability"]},
    {"pattern": "prometheus/**", "docs": ["02-observability"]},
]

IGNORED_SOURCE_PARTS = {
    ".git",
    "_build",
    "_generated",
    "__pycache__",
    "node_modules",
    "target",
    "dist",
}

DEFAULT_CONFIG_CANDIDATES = [
    ".pkb-source-doc-map.json",
    "doc/pkb-source-doc-map.json",
    "man/pkb-source-doc-map.json",
]

_DIRTY_PATHS_CACHE: Dict[str, set[str]] = {}


@dataclass
class StaleEntry:
    doc_page: str
    source_file: str
    source_mtime: datetime
    doc_mtime: datetime
    days_behind: int
    level: str


def git_last_modified(path: str, repo_root: str) -> Optional[datetime]:
    """Get the last git commit timestamp for a file."""
    try:
        result = subprocess.run(
            ["git", "log", "-1", "--format=%aI", "--", path],
            capture_output=True,
            text=True,
            cwd=repo_root,
            timeout=10,
        )
        if result.returncode == 0 and result.stdout.strip():
            return datetime.fromisoformat(result.stdout.strip())
    except (subprocess.TimeoutExpired, FileNotFoundError, ValueError):
        pass
    return None


def git_pattern_last_modified(pattern: str, repo_root: str) -> Optional[datetime]:
    """Get the latest commit time for a Git pathspec."""
    try:
        result = subprocess.run(
            ["git", "log", "-1", "--format=%aI", "--", pattern],
            capture_output=True,
            text=True,
            cwd=repo_root,
            timeout=10,
        )
        if result.returncode == 0 and result.stdout.strip():
            return datetime.fromisoformat(result.stdout.strip())
    except (subprocess.TimeoutExpired, FileNotFoundError, ValueError):
        pass
    return None


def fs_last_modified(path: str) -> Optional[datetime]:
    """Get filesystem mtime."""
    try:
        return datetime.fromtimestamp(os.path.getmtime(path), tz=timezone.utc)
    except OSError:
        return None


def worktree_is_dirty(repo_root: str, path: str) -> bool:
    """Return whether a path has uncommitted or untracked changes."""
    if repo_root not in _DIRTY_PATHS_CACHE:
        try:
            result = subprocess.run(
                ["git", "status", "--porcelain", "--untracked-files=all"],
                capture_output=True,
                text=True,
                cwd=repo_root,
                timeout=30,
            )
            dirty_paths = set()
            if result.returncode == 0:
                for line in result.stdout.splitlines():
                    if len(line) < 4:
                        continue
                    dirty_path = line[3:]
                    if " -> " in dirty_path:
                        dirty_path = dirty_path.rsplit(" -> ", 1)[-1]
                    dirty_paths.add(os.path.normpath(dirty_path))
            _DIRTY_PATHS_CACHE[repo_root] = dirty_paths
        except (subprocess.TimeoutExpired, FileNotFoundError):
            _DIRTY_PATHS_CACHE[repo_root] = set()

    rel_path = os.path.normpath(os.path.relpath(path, repo_root))
    return rel_path in _DIRTY_PATHS_CACHE[repo_root]


def last_modified(path: str, repo_root: str, use_git: bool) -> Optional[datetime]:
    """Use working-tree time for dirty files, otherwise commit time."""
    filesystem_time = fs_last_modified(path)
    if not use_git:
        return filesystem_time

    if worktree_is_dirty(repo_root, path):
        return filesystem_time or git_last_modified(os.path.relpath(path, repo_root), repo_root)

    return git_last_modified(os.path.relpath(path, repo_root), repo_root) or filesystem_time


def latest_source_modified(
    pattern: str,
    source_files: List[str],
    repo_root: str,
    use_git: bool,
) -> Tuple[Optional[datetime], str]:
    """Find the newest source time without one Git process per source file."""
    candidates = [
        (fs_last_modified(os.path.join(repo_root, rel_path)), rel_path)
        for rel_path in source_files
        if worktree_is_dirty(repo_root, os.path.join(repo_root, rel_path))
    ]
    candidates = [(mtime, rel_path) for mtime, rel_path in candidates if mtime is not None]
    if not use_git:
        candidates = [
            (fs_last_modified(os.path.join(repo_root, rel_path)), rel_path)
            for rel_path in source_files
        ]
        candidates = [(mtime, rel_path) for mtime, rel_path in candidates if mtime is not None]
        return max(candidates, key=lambda item: item[0], default=(None, pattern))

    git_time = git_pattern_last_modified(pattern, repo_root)
    if git_time is not None:
        candidates.append((git_time, pattern))
    return max(candidates, key=lambda item: item[0], default=(None, pattern))


def find_files_by_glob(pattern: str, repo_root: str) -> List[str]:
    """Expand glob pattern relative to repo root."""
    full_pattern = os.path.join(repo_root, pattern)
    matches = glob(full_pattern, recursive=True)
    files = []
    for match in matches:
        if not os.path.isfile(match):
            continue
        relative = os.path.relpath(match, repo_root)
        if any(part in IGNORED_SOURCE_PARTS for part in relative.split(os.sep)):
            continue
        files.append(relative)
    return sorted(files)


def detect_level(days_behind: int) -> str:
    """Convert age delta to report severity."""
    if days_behind <= 3:
        return "info"
    if days_behind <= 14:
        return "warning"
    return "critical"


def load_source_doc_map(repo_root: str, config_path: Optional[str]) -> List[dict]:
    """Load a project-specific map if provided, otherwise use defaults."""
    candidates: Iterable[str]
    if config_path:
        candidates = [config_path]
    else:
        candidates = DEFAULT_CONFIG_CANDIDATES

    for candidate in candidates:
        candidate_path = candidate
        if not os.path.isabs(candidate_path):
            candidate_path = os.path.join(repo_root, candidate_path)
        if not os.path.isfile(candidate_path):
            continue
        with open(candidate_path, "r", encoding="utf-8") as handle:
            loaded = json.load(handle)
        normalized = []
        for item in loaded:
            pattern = item.get("pattern")
            docs = item.get("docs", [])
            if not pattern or not isinstance(docs, list):
                raise ValueError(f"Invalid rule in {candidate_path}: {item}")
            normalized.append({"pattern": pattern, "docs": docs})
        return normalized

    return DEFAULT_SOURCE_DOC_MAP


def collect_doc_mtimes(repo_root: str, doc_dir: str, use_git: bool) -> Dict[str, datetime]:
    """Collect mtimes for PKB pages under the topic directories."""
    doc_path = Path(doc_dir if os.path.isabs(doc_dir) else os.path.join(repo_root, doc_dir))
    doc_mtimes: Dict[str, datetime] = {}
    if not os.path.isdir(doc_path):
        return doc_mtimes

    for abs_path in doc_path.rglob("*.md"):
        if any(part in {"_build", "_generated", "locale"} for part in abs_path.relative_to(doc_path).parts):
            continue
        name = abs_path.name
        mtime = last_modified(abs_path, repo_root, use_git)
        if mtime is not None:
            doc_mtimes[name[:-3]] = mtime
    return doc_mtimes


def detect_staleness(
    repo_root: str,
    doc_dir: str = "man",
    use_git: bool = True,
    config_path: Optional[str] = None,
) -> List[StaleEntry]:
    """Scan source files and compare timestamps against doc pages."""
    doc_mtimes = collect_doc_mtimes(repo_root, doc_dir, use_git)
    if not doc_mtimes:
        print(
            f"Warning: PKB doc directory not found or empty: {os.path.join(repo_root, doc_dir)}",
            file=sys.stderr,
        )
        return []

    rules = load_source_doc_map(repo_root, config_path)
    results: List[StaleEntry] = []

    for rule in rules:
        affected_docs = rule["docs"]
        if not affected_docs:
            continue

        source_files = find_files_by_glob(rule["pattern"], repo_root)
        if not source_files:
            continue
        src_mtime, src_file = latest_source_modified(
            rule["pattern"], source_files, repo_root, use_git
        )
        if src_mtime is None:
            continue

        for doc_page in affected_docs:
            doc_mtime = doc_mtimes.get(doc_page)
            if doc_mtime is None or src_mtime <= doc_mtime:
                continue
            days_behind = max(0, (src_mtime - doc_mtime).days)
            results.append(
                StaleEntry(
                    doc_page=doc_page,
                    source_file=src_file,
                    source_mtime=src_mtime,
                    doc_mtime=doc_mtime,
                    days_behind=days_behind,
                    level=detect_level(days_behind),
                )
            )

    deduped: Dict[tuple, StaleEntry] = {}
    for entry in results:
        key = (entry.doc_page, entry.source_file)
        existing = deduped.get(key)
        if existing is None or entry.days_behind > existing.days_behind:
            deduped[key] = entry

    return sorted(deduped.values(), key=lambda item: (-item.days_behind, item.doc_page, item.source_file))


def serialize_entries(entries: List[StaleEntry]) -> List[dict]:
    return [
        {
            "doc_page": entry.doc_page,
            "source_file": entry.source_file,
            "days_behind": entry.days_behind,
            "level": entry.level,
            "source_mtime": entry.source_mtime.isoformat(),
            "doc_mtime": entry.doc_mtime.isoformat(),
        }
        for entry in entries
    ]


def render_markdown_report(entries: List[StaleEntry]) -> str:
    if not entries:
        return "All tracked PKB pages are up-to-date.\n"

    lines = [
        "## Staleness Report",
        "",
        "| Level | Days Behind | Doc Page | Source File |",
        "|-------|-------------|----------|-------------|",
    ]
    for entry in entries:
        lines.append(
            f"| `{entry.level}` | `{entry.days_behind}` | `{entry.doc_page}` | `{entry.source_file}` |"
        )
    lines.append("")
    return "\n".join(lines)


def print_report(entries: List[StaleEntry], as_json: bool = False) -> None:
    """Print the staleness report."""
    if as_json:
        print(json.dumps(serialize_entries(entries), indent=2))
        return

    if not entries:
        print("All tracked PKB pages are up-to-date!")
        return

    icons = {"info": "INFO", "warning": "WARN", "critical": "CRIT"}
    print(f"\nPKB Staleness Report - {len(entries)} item(s)\n")
    print(f"{'Level':<10} {'Days':>5}  {'Doc Page':<25} {'Source File'}")
    print("-" * 96)
    for entry in entries:
        icon = icons.get(entry.level, "INFO")
        print(
            f"[{icon}] {entry.level:<7} {entry.days_behind:>5}d  "
            f"{entry.doc_page:<25} {entry.source_file}"
        )

    critical = [entry for entry in entries if entry.level == "critical"]
    warning = [entry for entry in entries if entry.level == "warning"]
    info = [entry for entry in entries if entry.level == "info"]
    print(f"\nSummary: {len(critical)} critical, {len(warning)} warning, {len(info)} info")

    if critical:
        print("\nCritical pages needing immediate review:")
        for doc_page in sorted({entry.doc_page for entry in critical}):
            print(f"  - {doc_page}.md")


def main() -> None:
    parser = argparse.ArgumentParser(description="PKB Staleness Checker")
    parser.add_argument("--repo-root", default=".", help="Repository root path")
    parser.add_argument("--doc-dir", default="man", help="Doc directory relative to repo root")
    parser.add_argument("--config", help="Optional JSON mapping file for source-to-doc rules")
    parser.add_argument("--no-git", action="store_true", help="Use filesystem mtime instead of git time")
    parser.add_argument("--json", action="store_true", help="Output JSON format")
    parser.add_argument("--output-file", help="Optional report artifact file")
    args = parser.parse_args()

    repo_root = os.path.abspath(args.repo_root)
    entries = detect_staleness(
        repo_root=repo_root,
        doc_dir=args.doc_dir,
        use_git=not args.no_git,
        config_path=args.config,
    )
    if args.output_file:
        payload = serialize_entries(entries)
        provenance = [args.doc_dir]
        if args.config:
            provenance.append(args.config)
        needs_input = [
            "decide which stale pages need immediate refresh versus batched update in the next PR, sprint, or release",
            "for each flagged page, choose whether a deterministic artifact is sufficient or a targeted LLM pass is needed",
        ]
        artifact = (
            render_json_artifact(
                script_name=script_id(__file__),
                payload=payload,
                provenance=provenance,
                needs_input=needs_input,
            )
            if args.json
            else render_markdown_artifact(
                script_name=script_id(__file__),
                title="PKB Staleness Report",
                provenance=provenance,
                body=render_markdown_report(entries),
                needs_input=needs_input,
            )
        )
        write_text_file(args.output_file, artifact)
        print(f"Wrote {args.output_file}")
    else:
        print_report(entries, as_json=args.json)

    if any(entry.level == "critical" for entry in entries):
        sys.exit(2)
    if any(entry.level == "warning" for entry in entries):
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()

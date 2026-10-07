#!/usr/bin/env python3
"""
PKB index generator.

Scans a PKB docs directory and produces an `index.md` candidate using the
numbered topic-directory layout. This is deterministic and safe to run in CI
or as a local helper.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from datetime import date
from typing import Iterable, List

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from generated_output import generated_output_path, render_markdown_artifact, script_id, write_text_file

SECTIONS = [
    ("Getting Started", ["0.getting-started/index"]),
    ("Architecture & Internals", ["1.architecture/index"]),
    ("Signaling", ["2.signal/index"]),
    ("Media", ["3.media/index"]),
    ("Development", ["4.development/index"]),
    ("Operations", ["5.operations/index"]),
    ("Appendix", ["7.appendix/index"]),
]


def slug_to_title(slug: str) -> str:
    parts = slug.replace("_", "-").split("-")
    return " ".join(part.capitalize() for part in parts if part)


def detect_title(doc_dir: str, explicit_title: str | None) -> str:
    if explicit_title:
        return explicit_title
    repo_name = os.path.basename(os.path.abspath(os.path.join(doc_dir, os.pardir)))
    return f"{slug_to_title(repo_name)} Knowledge Base"


def git_head_short(doc_dir: str) -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=doc_dir,
            capture_output=True,
            text=True,
            timeout=10,
            check=True,
        )
        return result.stdout.strip()
    except Exception:
        return "[git rev-parse --short HEAD]"


def existing_docs(doc_dir: str) -> set[str]:
    return {
        os.path.relpath(path, doc_dir)[:-3].replace(os.sep, "/")
        for root, _, names in os.walk(doc_dir)
        for name in names
        if name.endswith(".md")
        for path in [os.path.join(root, name)]
        if not any(part in {"_build", "_generated", "locale"} for part in path.split(os.sep))
    }


def doc_exists(doc_dir: str, docs: set[str], entry: str) -> bool:
    return entry in docs or os.path.isfile(os.path.join(doc_dir, f"{entry}.md"))


def emit_toctree(caption: str, entries: Iterable[str]) -> str:
    joined = "\n".join(entries)
    return (
        "```{toctree}\n"
        ":maxdepth: 2\n"
        f":caption: {caption}\n\n"
        f"{joined}\n"
        "```\n"
    )


def build_index(doc_dir: str, title: str) -> str:
    docs = existing_docs(doc_dir)
    blocks: List[str] = [
        f"# {title}",
        "",
        "<!-- maintained-by: human+ai -->",
        "",
        "This directory is the Project Knowledge Base for the repository.",
        "",
    ]

    for caption, ordered_entries in SECTIONS:
        present = [entry for entry in ordered_entries if doc_exists(doc_dir, docs, entry)]
        if present:
            blocks.append(emit_toctree(caption, present))

    blocks.extend(
        [
            "---",
            "<!-- PKB-metadata",
            f"last_updated: {date.today().isoformat()}",
            f"commit: {git_head_short(doc_dir)}",
            "updated_by: human+ai",
            "-->",
            "",
        ]
    )
    return "\n".join(blocks)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate a PKB index.md candidate")
    parser.add_argument("--doc-dir", default="man", help="PKB doc directory")
    parser.add_argument("--title", help="Optional explicit index title")
    parser.add_argument("--stdout", action="store_true", help="Print raw generated index candidate")
    parser.add_argument("--output-file", help="Optional sidecar artifact file")
    args = parser.parse_args()

    doc_dir = os.path.abspath(args.doc_dir)
    if not os.path.isdir(doc_dir):
        raise SystemExit(f"Doc directory not found: {doc_dir}")

    content = build_index(doc_dir, detect_title(doc_dir, args.title))
    if args.stdout:
        print(content)
        return

    target = args.output_file or generated_output_path(doc_dir, "index.generated.md")
    artifact = render_markdown_artifact(
        script_name=script_id(__file__),
        title="Index Regeneration Candidate",
        provenance=[os.path.relpath(doc_dir, os.getcwd()) if not os.path.isabs(args.doc_dir) else doc_dir],
        body="\n".join(
            [
                "## Proposed `index.md`",
                "",
                "```md",
                content.rstrip(),
                "```",
            ]
        ),
        needs_input=[
            "confirm the reader-facing project title if the repo slug is not the intended display name",
            "review whether any current topic-directory ordering intentionally deviates from the numbered layout",
        ],
    )
    write_text_file(target, artifact)
    print(f"Wrote {target}")


if __name__ == "__main__":
    main()

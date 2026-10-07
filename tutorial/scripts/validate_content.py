#!/usr/bin/env python3
"""Validate the FreeSWITCH 30-day tutorial manifest and lesson contract."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


LESSON_ID_RE = re.compile(r"^day-(\d{2})$")
CHECK_ID_RE = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
MARKDOWN_LINK_RE = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
FORBIDDEN_PLACEHOLDERS = (
    "TBD",
    "TODO",
    "NEEDS INPUT",
    "待补充",
    "待完成",
    "占位内容",
)
REQUIRED_SECTIONS = (
    "今日成果",
    "核心原理",
    "源码导航",
    "引导实验",
    "独立挑战",
    "验收",
    "故障排查",
    "中英术语表",
)
VALIDATION_MODES = {"automatic", "semi-automatic", "manual"}
EXPECTED_PHASES = (
    ("foundations", 1, 5),
    ("sip-media", 6, 10),
    ("webrtc-audio", 11, 15),
    ("ivr-esl", 16, 20),
    ("module-development", 21, 25),
    ("metrics-capstone", 26, 30),
)


def _safe_path(root: Path, value: Any, label: str, errors: list[str]) -> Path | None:
    if not isinstance(value, str) or not value:
        errors.append(f"{label}: expected a non-empty relative path")
        return None
    candidate = Path(value)
    if candidate.is_absolute():
        errors.append(f"{label}: absolute paths are not allowed: {value}")
        return None
    resolved_root = root.resolve()
    resolved = (root / candidate).resolve()
    if not resolved.is_relative_to(resolved_root):
        errors.append(f"{label}: path escapes tutorial root: {value}")
        return None
    return resolved


def _section_bodies(text: str) -> dict[str, str]:
    bodies: dict[str, list[str]] = {}
    current: str | None = None
    for line in text.splitlines():
        if line.startswith("## "):
            current = line[3:].strip()
            bodies.setdefault(current, [])
        elif current is not None:
            bodies[current].append(line)
    return {name: "\n".join(lines).strip() for name, lines in bodies.items()}


def validate_lesson(path: Path, tutorial_root: Path) -> list[str]:
    errors: list[str] = []
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        return [f"{path}: cannot read lesson: {exc}"]

    if not re.search(r"^#\s+第\s*\d+\s*天[：:]", text, re.MULTILINE):
        errors.append(f"{path}: missing '# 第 N 天：标题' heading")

    bodies = _section_bodies(text)
    for section in REQUIRED_SECTIONS:
        if section not in bodies:
            errors.append(f"{path}: missing required section '## {section}'")
        elif not bodies[section]:
            errors.append(f"{path}: required section '## {section}' is empty")

    upper_text = text.upper()
    for marker in FORBIDDEN_PLACEHOLDERS:
        if marker.upper() in upper_text:
            errors.append(f"{path}: forbidden placeholder '{marker}'")

    for raw_target in MARKDOWN_LINK_RE.findall(text):
        target = raw_target.strip().split(maxsplit=1)[0].strip("<>")
        if not target or target.startswith("#") or "://" in target or target.startswith("mailto:"):
            continue
        target = target.split("#", 1)[0]
        if not target:
            continue
        resolved = (path.parent / target).resolve()
        if not resolved.is_relative_to(tutorial_root.parent.resolve()):
            errors.append(f"{path}: link escapes repository root: {raw_target}")
        elif not resolved.exists():
            errors.append(f"{path}: linked path does not exist: {raw_target}")
    return errors


def validate_manifest(
    manifest_path: Path,
    tutorial_root: Path | None = None,
    *,
    allow_missing_lessons: bool = False,
) -> list[str]:
    errors: list[str] = []
    tutorial_root = (tutorial_root or manifest_path.parent.parent).resolve()

    try:
        document = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"{manifest_path}: cannot load manifest: {exc}"]

    if not isinstance(document, dict):
        return [f"{manifest_path}: manifest root must be an object"]
    if document.get("schema_version") != 1:
        errors.append("manifest: schema_version must be 1")
    if document.get("default_locale") != "zh-CN":
        errors.append("manifest: default_locale must be zh-CN")

    locales = document.get("locales")
    locale_ids: set[str] = set()
    if not isinstance(locales, list) or not locales:
        errors.append("manifest: locales must be a non-empty array")
    else:
        for index, locale in enumerate(locales):
            if not isinstance(locale, dict) or not isinstance(locale.get("id"), str):
                errors.append(f"locales[{index}]: invalid locale")
                continue
            locale_id = locale["id"]
            if locale_id in locale_ids:
                errors.append(f"locales[{index}]: duplicate locale '{locale_id}'")
            locale_ids.add(locale_id)
            fallback = locale.get("fallback")
            if fallback is not None and fallback == locale_id:
                errors.append(f"locales[{index}]: locale cannot fall back to itself")
        if "zh-CN" not in locale_ids:
            errors.append("manifest: zh-CN locale is required")
        for index, locale in enumerate(locales):
            if isinstance(locale, dict) and locale.get("fallback") not in (None, *locale_ids):
                errors.append(f"locales[{index}]: unknown fallback '{locale.get('fallback')}'")

    phases = document.get("phases")
    phase_ranges: dict[str, tuple[int, int]] = {}
    if not isinstance(phases, list):
        errors.append("manifest: phases must be an array")
    else:
        for index, phase in enumerate(phases):
            if not isinstance(phase, dict):
                errors.append(f"phases[{index}]: invalid phase")
                continue
            phase_id = phase.get("id")
            days = phase.get("days")
            if not isinstance(phase_id, str) or phase_id in phase_ranges:
                errors.append(f"phases[{index}]: missing or duplicate phase id")
                continue
            if not (
                isinstance(days, list)
                and len(days) == 2
                and all(isinstance(day, int) for day in days)
                and days[0] <= days[1]
            ):
                errors.append(f"phases[{index}]: days must be [start, end]")
                continue
            phase_ranges[phase_id] = (days[0], days[1])
        actual_phases = tuple((pid, *phase_ranges.get(pid, (-1, -1))) for pid, _, _ in EXPECTED_PHASES)
        if actual_phases != EXPECTED_PHASES or set(phase_ranges) != {p[0] for p in EXPECTED_PHASES}:
            errors.append("manifest: phases must cover the six exact five-day ranges")

    lessons = document.get("lessons")
    if not isinstance(lessons, list):
        return errors + ["manifest: lessons must be an array"]
    if len(lessons) != 30:
        errors.append(f"manifest: expected 30 lessons, found {len(lessons)}")

    lesson_ids: set[str] = set()
    check_ids: set[str] = set()
    lesson_days: dict[str, int] = {}
    normalized_lessons: list[dict[str, Any]] = []

    for index, lesson in enumerate(lessons):
        label = f"lessons[{index}]"
        if not isinstance(lesson, dict):
            errors.append(f"{label}: lesson must be an object")
            continue
        normalized_lessons.append(lesson)
        lesson_id = lesson.get("id")
        day = lesson.get("day")
        match = LESSON_ID_RE.fullmatch(lesson_id) if isinstance(lesson_id, str) else None
        if match is None:
            errors.append(f"{label}: invalid lesson id '{lesson_id}'")
        elif lesson_id in lesson_ids:
            errors.append(f"{label}: duplicate lesson id '{lesson_id}'")
        else:
            lesson_ids.add(lesson_id)
            if isinstance(day, int):
                lesson_days[lesson_id] = day
            if day != int(match.group(1)):
                errors.append(f"{label}: id and day do not match")
        expected_day = index + 1
        if day != expected_day:
            errors.append(f"{label}: expected day {expected_day}, found {day}")

        phase_id = lesson.get("phase")
        expected_phase = next((pid for pid, start, end in EXPECTED_PHASES if start <= expected_day <= end), None)
        if phase_id != expected_phase:
            errors.append(f"{label}: day {expected_day} must use phase '{expected_phase}'")
        if not isinstance(lesson.get("title_zh"), str) or not lesson["title_zh"].strip():
            errors.append(f"{label}: title_zh must be non-empty")

        lesson_path = _safe_path(tutorial_root, lesson.get("path"), f"{label}.path", errors)
        if lesson_path is not None:
            if not lesson_path.exists():
                if not allow_missing_lessons:
                    errors.append(f"{label}: lesson file does not exist: {lesson.get('path')}")
            elif lesson_path.name != "_template.md":
                errors.extend(validate_lesson(lesson_path, tutorial_root))

        assets = lesson.get("assets")
        if not isinstance(assets, list) or not assets:
            errors.append(f"{label}: assets must be a non-empty array")
        else:
            for asset_index, asset in enumerate(assets):
                asset_path = _safe_path(tutorial_root, asset, f"{label}.assets[{asset_index}]", errors)
                if asset_path is not None and not asset_path.exists():
                    errors.append(f"{label}: asset does not exist: {asset}")

        checks = lesson.get("checks")
        if not isinstance(checks, list) or not checks:
            errors.append(f"{label}: checks must be a non-empty array")
        else:
            for check_index, check in enumerate(checks):
                check_label = f"{label}.checks[{check_index}]"
                if not isinstance(check, dict):
                    errors.append(f"{check_label}: check must be an object")
                    continue
                check_id = check.get("id")
                if not isinstance(check_id, str) or CHECK_ID_RE.fullmatch(check_id) is None:
                    errors.append(f"{check_label}: invalid check id '{check_id}'")
                elif check_id in check_ids:
                    errors.append(f"{check_label}: duplicate check id '{check_id}'")
                else:
                    check_ids.add(check_id)
                if check.get("mode") not in VALIDATION_MODES:
                    errors.append(f"{check_label}: invalid validation mode '{check.get('mode')}'")

    for index, lesson in enumerate(normalized_lessons):
        current_day = lesson.get("day")
        prerequisites = lesson.get("prerequisites")
        if not isinstance(prerequisites, list):
            errors.append(f"lessons[{index}]: prerequisites must be an array")
            continue
        for prerequisite in prerequisites:
            if prerequisite not in lesson_ids:
                errors.append(f"lessons[{index}]: unknown prerequisite '{prerequisite}'")
            elif isinstance(current_day, int) and lesson_days.get(prerequisite, current_day) >= current_day:
                errors.append(f"lessons[{index}]: prerequisite '{prerequisite}' is not earlier")

    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--tutorial-root", type=Path)
    parser.add_argument(
        "--allow-missing-lessons",
        action="store_true",
        help="validate manifest structure before all lesson files are authored",
    )
    args = parser.parse_args(argv)
    errors = validate_manifest(
        args.manifest,
        args.tutorial_root,
        allow_missing_lessons=args.allow_missing_lessons,
    )
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print(f"Tutorial content validation failed with {len(errors)} error(s).", file=sys.stderr)
        return 1
    print("Tutorial content validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

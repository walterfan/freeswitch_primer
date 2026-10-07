from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path


TUTORIAL_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TUTORIAL_ROOT))

from scripts.validate_content import validate_manifest  # noqa: E402


LESSON_BODY = """# 第 {day} 天：测试课程

## 今日成果

完成一个可观察结果。

## 核心原理

理解本课的最小理论。

## 源码导航

阅读 [测试参考](../../../reference.md)。

## 引导实验

执行可重复的实验并检查结果。

## 独立挑战

修改一个条件并解释现象。

## 验收

**验收方式：自动**，检查输出与预期一致。

## 故障排查

从证据定位原因并恢复配置。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 会话 | Session | 一次通话执行上下文 |
"""


class ContentValidatorTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "content/zh-CN/days").mkdir(parents=True)
        (self.root / "reference.md").write_text("# Fixture reference\n", encoding="utf-8")
        source_manifest = json.loads(
            (TUTORIAL_ROOT / "content/manifest.json").read_text(encoding="utf-8")
        )
        self.document = source_manifest
        for lesson in self.document["lessons"]:
            (self.root / lesson["assets"][0]).mkdir(parents=True)
            (self.root / lesson["path"]).write_text(
                LESSON_BODY.format(day=lesson["day"]), encoding="utf-8"
            )
        self.manifest = self.root / "content/manifest.json"
        self.write_manifest()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def write_manifest(self) -> None:
        self.manifest.write_text(
            json.dumps(self.document, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )

    def validate(self) -> list[str]:
        return validate_manifest(self.manifest, self.root)

    def test_valid_manifest_and_lessons(self) -> None:
        self.assertEqual([], self.validate())

    def test_missing_lesson_is_rejected(self) -> None:
        self.document["lessons"].pop()
        self.write_manifest()
        self.assertTrue(any("expected 30 lessons" in error for error in self.validate()))

    def test_duplicate_lesson_id_is_rejected(self) -> None:
        self.document["lessons"][1]["id"] = "day-01"
        self.write_manifest()
        self.assertTrue(any("duplicate lesson id" in error for error in self.validate()))

    def test_out_of_order_lessons_are_rejected(self) -> None:
        lessons = self.document["lessons"]
        lessons[0], lessons[1] = lessons[1], lessons[0]
        self.write_manifest()
        self.assertTrue(any("expected day" in error for error in self.validate()))

    def test_unknown_prerequisite_is_rejected(self) -> None:
        self.document["lessons"][1]["prerequisites"] = ["day-99"]
        self.write_manifest()
        self.assertTrue(any("unknown prerequisite" in error for error in self.validate()))

    def test_empty_required_section_is_rejected(self) -> None:
        lesson_path = self.root / self.document["lessons"][0]["path"]
        lesson_path.write_text(
            LESSON_BODY.format(day=1).replace(
                "## 今日成果\n\n完成一个可观察结果。",
                "## 今日成果\n\n",
            ),
            encoding="utf-8",
        )
        self.assertTrue(any("今日成果' is empty" in error for error in self.validate()))

    def test_forbidden_placeholder_is_rejected(self) -> None:
        lesson_path = self.root / self.document["lessons"][0]["path"]
        lesson_path.write_text(
            LESSON_BODY.format(day=1) + "\nTODO: later\n", encoding="utf-8"
        )
        self.assertTrue(any("forbidden placeholder" in error for error in self.validate()))

    def test_unknown_check_id_shape_is_rejected(self) -> None:
        self.document["lessons"][0]["checks"][0]["id"] = "run arbitrary command"
        self.write_manifest()
        self.assertTrue(any("invalid check id" in error for error in self.validate()))

    def test_missing_asset_and_link_are_rejected(self) -> None:
        document = copy.deepcopy(self.document)
        document["lessons"][0]["assets"] = ["labs/missing"]
        self.document = document
        lesson_path = self.root / self.document["lessons"][0]["path"]
        lesson_path.write_text(
            LESSON_BODY.format(day=1).replace(
                "../../../reference.md", "../../../missing.md"
            ),
            encoding="utf-8",
        )
        self.write_manifest()
        errors = self.validate()
        self.assertTrue(any("asset does not exist" in error for error in errors))
        self.assertTrue(any("linked path does not exist" in error for error in errors))


if __name__ == "__main__":
    unittest.main()

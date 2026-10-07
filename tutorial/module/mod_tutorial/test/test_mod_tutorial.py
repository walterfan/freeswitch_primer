import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


MODULE_ROOT = Path(__file__).parents[1]


def validate_configuration(document):
    root = ET.fromstring(document)
    menus = root.find("menus")
    if menus is None:
        raise ValueError("menus is required")
    menu_nodes = menus.findall("menu")
    if not 1 <= len(menu_nodes) <= 8:
        raise ValueError("menu count is outside the bound")
    seen_menus = set()
    seen_choices = set()
    for menu in menu_nodes:
        name = menu.get("name", "")
        if not re.fullmatch(r"[A-Za-z0-9_-]{1,32}", name):
            raise ValueError("invalid menu")
        if name in seen_menus:
            raise ValueError("duplicate menu")
        seen_menus.add(name)
        choices = menu.findall("choice")
        if not choices:
            raise ValueError("a menu needs a choice")
        for choice in choices:
            digit = choice.get("digit", "")
            if not re.fullmatch(r"[0-9*#]", digit):
                raise ValueError("invalid choice")
            if (name, digit) in seen_choices:
                raise ValueError("duplicate choice")
            seen_choices.add((name, digit))
    if len(seen_choices) > 32:
        raise ValueError("choice count is outside the bound")


class TutorialModuleHarnessTests(unittest.TestCase):
    def setUp(self):
        self.source = (MODULE_ROOT / "mod_tutorial.c").read_text()
        self.logic = (MODULE_ROOT / "mod_tutorial_logic.c").read_text()
        self.config = (MODULE_ROOT / "mod_tutorial.conf.xml").read_text()
        self.dialplan = (MODULE_ROOT / "tutorial-ivr.xml").read_text()

    def test_configuration_and_bounds(self):
        validate_configuration(self.config)
        with self.assertRaises(ValueError):
            validate_configuration("<configuration><menus/></configuration>")
        with self.assertRaises(ValueError):
            validate_configuration(
                "<configuration><menus><menu name='bad menu'>"
                "<choice digit='1'/></menu></menus></configuration>"
            )
        with self.assertRaises(ValueError):
            validate_configuration(
                "<configuration><menus><menu name='main'>"
                "<choice digit='12'/></menu></menus></configuration>"
            )
        digits = "0123456789*#"
        menus = "".join(
            f"<menu name='menu{menu_index}'>"
            + "".join(f"<choice digit='{digit}'/>" for digit in digits)
            + "</menu>"
            for menu_index in range(3)
        )
        with self.assertRaises(ValueError):
            validate_configuration(
                f"<configuration><menus>{menus}</menus></configuration>"
            )

    def test_application_channel_and_event_contract(self):
        for variable in (
            "tutorial_ivr_menu",
            "tutorial_ivr_choice",
            "tutorial_ivr_recorded",
            "tutorial_ivr_error",
        ):
            self.assertIn(variable, self.source)
        for header in ("Menu", "Choice", "Unique-ID", "Event-Date-Timestamp"):
            self.assertIn(f'"{header}"', self.source)
        self.assertIn('TUTORIAL_EVENT "tutorial::ivr_choice"', self.source)
        self.assertIn("switch_event_create_subclass", self.source)

    def test_lifecycle_and_read_only_api_contract(self):
        for symbol in (
            "switch_mutex_init",
            "switch_mutex_destroy",
            "switch_event_reserve_subclass",
            "switch_event_free_subclass",
            "SWITCH_ADD_API",
            "SWITCH_ADD_APP",
        ):
            self.assertIn(symbol, self.source)
        self.assertIn("tutorial_metrics [text|json]", self.source)
        self.assertIn("switch_safe_free(input)", self.source)
        self.assertIn("tutorial_format_metrics", self.source)

    def test_tutorial_ivr_dialplan_contract(self):
        root = ET.fromstring(self.dialplan)
        applications = {action.get("application") for action in root.iter("action")}
        self.assertTrue({"answer", "read", "playback", "tutorial_ivr_metric", "hangup"}
                        <= applications)
        self.assertGreaterEqual(self.dialplan.count("<condition"), 4)
        self.assertIn("tutorial_main_digit", self.dialplan)
        self.assertIn("tutorial_submenu_digit", self.dialplan)
        self.assertNotIn("conf/vanilla", self.dialplan)
        self.assertIn("tutorial_record_choice", self.logic)


if __name__ == "__main__":
    unittest.main()

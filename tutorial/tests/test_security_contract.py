import re
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
TUTORIAL = ROOT / "tutorial"


class TutorialSecurityContractTests(unittest.TestCase):
    def test_default_http_and_metrics_bindings_are_loopback_scoped(self):
        for config_name in ("config.yaml", "localhost-https.yaml"):
            config = (TUTORIAL / "site" / "config" / config_name).read_text()
            self.assertRegex(config, r"host:\s+127\.0\.0\.1")
            self.assertIn("allow_remote: false", config)
        prometheus = (TUTORIAL / "deploy" / "prometheus.yml").read_text()
        self.assertIn('targets: ["127.0.0.1:7009"]', prometheus)

    def test_public_and_event_paths_keep_secrets_out_of_outputs(self):
        http_server = (TUTORIAL / "site" / "src" / "http_server.cpp").read_text()
        observer = (TUTORIAL / "site" / "src" / "esl_observer.cpp").read_text()
        browser = (TUTORIAL / "site" / "web" / "live_evidence.mjs").read_text()
        for source in (http_server, observer, browser):
            for forbidden in ("Authorization", "Private-Key", "password", "Proxy-Authorization"):
                self.assertNotIn(forbidden, source)
        self.assertIn("allowlisted", observer.lower())

    def test_validation_and_retention_are_bounded(self):
        checks = (TUTORIAL / "site" / "src" / "check_registry.cpp").read_text()
        observer = (TUTORIAL / "site" / "src" / "esl_observer.cpp").read_text()
        self.assertIn("valid_identifier", checks)
        self.assertIn("RecentEventRing", observer)
        self.assertIn("EventDeliveryQueue", observer)
        self.assertNotRegex(checks, r"system\s*\(")
        self.assertNotRegex(checks, r"popen\s*\(")

    def test_generated_tls_material_is_ignored_and_trace_is_not_enabled(self):
        ignore = (TUTORIAL / ".gitignore").read_text()
        self.assertIn("deploy/certs", ignore)
        configs = "\n".join(
            path.read_text()
            for path in (TUTORIAL / "site" / "config").glob("*.yaml")
        )
        self.assertNotIn("sip_trace", configs)
        self.assertNotIn("capture", configs)


if __name__ == "__main__":
    unittest.main()

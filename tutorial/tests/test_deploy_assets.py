import unittest
from pathlib import Path


TUTORIAL_ROOT = Path(__file__).parents[1]
DEPLOY_ROOT = TUTORIAL_ROOT / "deploy"


class DeployAssetTests(unittest.TestCase):
    def test_site_image_copies_content_assets_and_not_certificates(self):
        dockerfile = (TUTORIAL_ROOT / "site" / "Dockerfile").read_text()
        dockerignore = (TUTORIAL_ROOT / ".dockerignore").read_text()
        for asset in ("COPY content /src/content", "THIRD_PARTY_NOTICES.md", "EXPOSE 7009"):
            self.assertIn(asset, dockerfile)
        for secret_suffix in ("**/*.key", "**/*.pem", "deploy/certs/"):
            self.assertIn(secret_suffix, dockerignore)

    def test_compose_includes_baseline_and_mounts_tutorial_assets(self):
        compose = (DEPLOY_ROOT / "compose.yaml").read_text()
        self.assertIn("../../docker/examples/Debian11/compose.yaml", compose)
        self.assertIn("mod_tutorial.conf.xml", compose)
        self.assertIn("tutorial-ivr.xml", compose)
        self.assertIn("mod_tutorial.so", compose)
        self.assertIn("TUTORIAL_ESL_PASSWORD", compose)
        self.assertNotIn("conf/vanilla", compose)
        site_config = (DEPLOY_ROOT / "site-config.yaml").read_text()
        self.assertIn("host: 127.0.0.1", site_config)
        wrapper = (DEPLOY_ROOT / "tutorial-compose.sh").read_text()
        self.assertIn("TUTORIAL_ESL_PASSWORD", wrapper)
        self.assertIn("TUTORIAL_MODULE_SO", wrapper)
        self.assertIn("compose down --remove-orphans", wrapper)
        self.assertIn('rm -rf -- "${script_dir}/certs"', wrapper)
        self.assertNotIn("docker system prune", wrapper)

    def test_prometheus_scrapes_only_the_tutorial_service(self):
        prometheus = (DEPLOY_ROOT / "prometheus.yml").read_text()
        self.assertIn("job_name: freeswitch-tutorial", prometheus)
        self.assertIn("metrics_path: /metrics", prometheus)
        self.assertIn('targets: ["127.0.0.1:7009"]', prometheus)
        self.assertNotIn("freeswitch:8021", prometheus)


if __name__ == "__main__":
    unittest.main()

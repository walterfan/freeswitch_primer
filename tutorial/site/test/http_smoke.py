#!/usr/bin/env python3
"""Black-box smoke tests for the tutorial service HTTP contract."""

from __future__ import annotations

import http.client
import json
import pathlib
import socket
import subprocess
import sys
import tempfile
import time


def free_port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def request(port: int, method: str, path: str, body: str | None = None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
    connection.request(method, path, body=body)
    response = connection.getresponse()
    payload = response.read().decode("utf-8")
    headers = {key.lower(): value for key, value in response.getheaders()}
    connection.close()
    return response.status, headers, payload


def wait_until_ready(port: int, process: subprocess.Popen[str]) -> None:
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if process.poll() is not None:
            stdout, stderr = process.communicate()
            raise AssertionError(f"service exited early: {stdout}\n{stderr}")
        try:
            if request(port, "GET", "/api/v1/health")[0] == 200:
                return
        except OSError:
            time.sleep(0.05)
    raise AssertionError("service did not become ready")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: http_smoke.py <service-binary>")
    binary = pathlib.Path(sys.argv[1]).resolve()
    port = free_port()
    with tempfile.TemporaryDirectory(prefix="fstutorial-smoke-") as directory:
        root = pathlib.Path(directory)
        web = root / "web"
        content = root / "content"
        (content / "zh-CN" / "days").mkdir(parents=True)
        web.mkdir()
        (web / "index.html").write_text("<h1>FreeSWITCH 30 天</h1>", encoding="utf-8")
        (web / "app.mjs").write_text("console.log('tutorial');", encoding="utf-8")
        (content / "zh-CN" / "days" / "day-01.md").write_text(
            "# 第 1 天：系统模型\n", encoding="utf-8")
        config = root / "config.yaml"
        config.write_text(
            f"""http:
  host: 127.0.0.1
  port: {port}
content_root: {content}
web_root: {web}
default_locale: zh-CN
sip:
  wss_url: wss://127.0.0.1:7443
  domain: 127.0.0.1
  tutorial_extensions: [\"1000\", \"1001\", \"5000\"]
esl:
  enabled: false
  password_env: FS_TUTORIAL_ESL_PASSWORD
""",
            encoding="utf-8",
        )
        process = subprocess.Popen(
            [str(binary), "--config", str(config)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            wait_until_ready(port, process)

            status, headers, body = request(port, "GET", "/")
            assert status == 200 and "FreeSWITCH 30" in body
            assert headers["content-type"].startswith("text/html")
            assert headers["cache-control"] == "no-cache"

            status, headers, body = request(port, "GET", "/assets/app.mjs")
            assert status == 200 and headers["content-type"].startswith("text/javascript")
            assert "max-age=3600" in headers["cache-control"]

            status, headers, body = request(port, "GET", "/api/v1/lessons/en-US/day-01")
            assert status == 200 and "系统模型" in body
            assert headers["content-language"] == "zh-CN"
            assert headers["x-tutorial-locale-fallback"] == "zh-CN"

            status, _, _ = request(port, "GET", "/content/%2e%2e/config.yaml")
            assert status in (400, 404)

            status, headers, body = request(port, "GET", "/api/v1/public-config")
            public_config = json.loads(body)
            assert status == 200 and headers["cache-control"] == "no-store"
            assert set(public_config) == {"default_locale", "sip"}
            assert set(public_config["sip"]) == {"wss_url", "domain", "tutorial_extensions"}
            assert "password" not in body.lower() and "esl" not in body.lower()

            status, _, body = request(port, "GET", "/api/v1/health")
            health = json.loads(body)
            assert status == 200 and health["status"] == "unavailable"
            assert health["components"]["esl"]["status"] == "unavailable"

            status, headers, body = request(port, "GET", "/metrics")
            assert status == 200 and headers["content-type"].startswith("text/plain")
            for family in (
                "freeswitch_up", "freeswitch_esl_connected", "freeswitch_sessions_current",
                "freeswitch_sessions_total", "freeswitch_calls_total",
                "freeswitch_call_duration_seconds", "freeswitch_heartbeat_age_seconds",
                "freeswitch_ivr_choice_total",
            ):
                assert f"# HELP {family}" in body and f"# TYPE {family}" in body
            assert "freeswitch_up 0" in body

            status, headers, body = request(port, "GET", "/api/v1/events")
            assert status == 200 and headers["content-type"].startswith("text/event-stream")
            assert "retry: 3000" in body and ": keepalive" in body

            status, _, body = request(port, "POST", "/api/v1/checks/day-01/unknown")
            assert status == 404 and "unknown" in body
            status, _, body = request(port, "POST", "/api/v1/checks/day-01/service-health")
            assert status == 200 and json.loads(body)["status"] == "unavailable"
            for lesson_id, check_id in (
                ("day-02", "freeswitch-status"),
                ("day-03", "module-inventory"),
                ("day-07", "sip-registration"),
                ("day-16", "ivr-sounds"),
                ("day-22", "module-api"),
            ):
                status, _, body = request(
                    port, "POST", f"/api/v1/checks/{lesson_id}/{check_id}")
                assert status == 200 and json.loads(body)["status"] == "unavailable"
            status, _, _ = request(
                port, "POST", "/api/v1/checks/day-01/service-health", "status; shutdown")
            assert status == 400
            status, _, _ = request(port, "POST", "/api/v1/checks/day-01/%2e%2e%2fstatus")
            assert status in (400, 404)
        finally:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

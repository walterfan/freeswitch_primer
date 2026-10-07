import assert from "node:assert/strict";
import {createHash} from "node:crypto";
import {readFile} from "node:fs/promises";
import test from "node:test";
import {fileURLToPath} from "node:url";
import {MARKDOWN_IT_VERSION, renderMarkdown} from "../web/markdown.mjs";
import {ProgressStore, STORAGE_KEY} from "../web/progress.mjs";
import {checkPath} from "../web/checks.mjs";
import {buildPreflight} from "../web/preflight.mjs";
import {SipAdapter, SIP_JS_VERSION} from "../web/sip_adapter.mjs";
import {SipStateMachine, SIP_STATES} from "../web/sip_state.mjs";
import {MediaDeviceController} from "../web/media_devices.mjs";
import {BoundedPanel, formatEvent, parseMetrics} from "../web/live_evidence.mjs";
import {groupLessonsByPhase, lessonHash, selectLesson} from "../web/course_map.mjs";
import {diagnoseFailure} from "../web/diagnostics.mjs";

class MemoryStorage { constructor() { this.values = new Map(); } getItem(key) { return this.values.get(key) ?? null; } setItem(key, value) { this.values.set(key, value); } }

test("native course shell exposes all manifest lessons and phase outcomes", async () => {
  const manifest = JSON.parse(await readFile(new URL("../../content/manifest.json", import.meta.url), "utf8"));
  const grouped = groupLessonsByPhase(manifest);
  assert.equal(manifest.lessons.length, 30);
  assert.equal([...grouped.values()].reduce((count, lessons) => count + lessons.length, 0), 30);
  assert.ok(manifest.phases.every(phase => phase.outcome_zh?.length > 0));
  for (const lesson of manifest.lessons) assert.equal(lessonHash(lesson.id), `#${lesson.id}`);
  assert.equal(selectLesson(manifest, "day-30").id, "day-30");
  assert.equal(selectLesson(manifest, "not-a-lesson").id, "day-01");
  const index = await readFile(new URL("../web/index.html", import.meta.url), "utf8");
  const app = await readFile(new URL("../web/app.mjs", import.meta.url), "utf8");
  assert.match(index, /id="course-map"/);
  assert.match(index, /id="lesson-reader"/);
  assert.match(index, /id="previous-lesson"/);
  assert.match(index, /id="next-lesson"/);
  assert.match(app, /groupLessonsByPhase/);
  assert.match(app, /phase\.outcome_zh/);
  assert.match(app, /lessonHash/);
});

test("progress survives a new store and stays locale independent", () => {
  const storage = new MemoryStorage(); new ProgressStore(storage).setCompleted("day-12", true);
  assert.deepEqual([...new ProgressStore(storage).completed()], ["day-12"]);
  assert.deepEqual(JSON.parse(storage.getItem(STORAGE_KEY)), {completed: ["day-12"]});
});
test("progress serialization cannot contain credentials", () => {
  const storage = new MemoryStorage(); const store = new ProgressStore(storage);
  assert.throws(() => store.setCompleted("password=hunter2", true)); store.setCompleted("day-01", true);
  const serialized = storage.getItem(STORAGE_KEY);
  for (const forbidden of ["password","authorization","sip","locale","hunter2"]) assert.equal(serialized.toLowerCase().includes(forbidden), false);
});
test("markdown renders CommonMark code and tables while escaping raw HTML", () => {
  const html = renderMarkdown([
    "# 标题",
    "",
    "1. first",
    "2. second",
    "",
    "| 中文 | English |",
    "| --- | --- |",
    "| 代码 | `code` |",
    "",
    "```c",
    "<unsafe>",
    "```",
    "",
    "[source](../../src/switch.c)",
    "",
    "<script>alert(1)</script>",
  ].join("\n"));
  assert.match(html, /<h1>标题<\/h1>/);
  assert.match(html, /<ol>[\s\S]*<li>first<\/li>/);
  assert.match(html, /<table>[\s\S]*<th>中文<\/th>[\s\S]*<td><code>code<\/code><\/td>/);
  assert.match(html, /<pre><code class="language-c">&lt;unsafe&gt;\n<\/code><\/pre>/);
  assert.match(html, /<a href="\.\.\/\.\.\/src\/switch\.c">source<\/a>/);
  assert.equal(html.includes("<script>"), false);
  assert.match(html, /&lt;script&gt;alert\(1\)&lt;\/script&gt;/);
});

test("vendored markdown-it release has deterministic version, hash, and local import", async () => {
  const vendorRoot = new URL("../web/vendor/markdown-it/", import.meta.url);
  const manifest = JSON.parse(await readFile(new URL("manifest.json", vendorRoot), "utf8"));
  const bundle = await readFile(new URL(manifest.asset, vendorRoot));
  assert.equal(manifest.version, MARKDOWN_IT_VERSION);
  assert.equal(manifest.version, "15.0.1");
  assert.equal(createHash("sha256").update(bundle).digest("hex"), manifest.sha256);
  assert.equal(manifest.license, "MIT");
  assert.match(await readFile(new URL(manifest.license_file, vendorRoot), "utf8"), /Permission is hereby granted/);
  const wrapper = await readFile(new URL("../web/markdown.mjs", import.meta.url), "utf8");
  assert.ok(wrapper.includes(`./vendor/markdown-it/${manifest.asset}`));
  assert.match(wrapper, /html:\s*false/);
  assert.equal(wrapper.includes("https://"), false);
});
test("only checks declared by the active lesson produce a request path", () => {
  const lesson = {id: "day-01", checks: [{id: "service-health", mode: "automatic"}]};
  assert.equal(checkPath(lesson, "service-health"), "/api/v1/checks/day-01/service-health");
  assert.throws(() => checkPath(lesson, "status;shutdown"));
  assert.throws(() => checkPath(lesson, "unknown"));
  assert.throws(() => checkPath({id: "../../etc", checks: [{id: "x"}]}, "x"));
});
test("preflight maps every stage failure to actionable remediation", () => {
  const stages = buildPreflight({}, {secureContext: false, mediaDevices: false, wssUrl: "ws://lab"});
  assert.deepEqual(stages.map(stage => stage.name), ["service","freeswitch","esl","heartbeat","sip_profile","secure_context","microphone","wss"]);
  for (const stage of stages) { assert.equal(stage.status, "unavailable"); assert.ok(stage.remediation.length > 12); }
  const healthy = buildPreflight({service: {status: "healthy", detail: "ok"}}, {secureContext: true, mediaDevices: true, wssUrl: "wss://localhost:7443"});
  assert.equal(healthy.find(stage => stage.name === "secure_context").status, "healthy");
  assert.equal(healthy.find(stage => stage.name === "microphone").status, "healthy");
  assert.equal(healthy.find(stage => stage.name === "wss").status, "healthy");
});

test("browser diagnostics map protocol failures to safe troubleshooting steps", () => {
  const fixtures = [
    ["secure_context", "window is not a secure context"],
    ["certificate", "certificate trust validation failed"],
    ["wss", "WebSocket connection refused"],
    ["sip_authentication", "SIP REGISTER received 401 digest challenge"],
    ["sdp", "SDP offer has no compatible audio section"],
    ["ice", "ICE candidate connectivity failed"],
    ["dtls_srtp", "DTLS-SRTP handshake failed"],
    ["codec", "no common codec payload type"],
    ["no_audio", "remote audio is muted"],
  ];
  for (const [stage, message] of fixtures) {
    const diagnostic = diagnoseFailure(new Error(message));
    assert.equal(diagnostic.stage, stage);
    assert.equal(diagnostic.status, "failed");
    assert.ok(diagnostic.remediation.length > 12);
    assert.equal(diagnostic.detail.includes(message), false);
  }
  assert.equal(diagnoseFailure({stage: "ice", error: new Error("secret")}).stage, "ice");
});

test("vendored SIP.js release has deterministic version and hash", async () => {
  const vendorRoot = new URL("../web/vendor/sip.js/", import.meta.url);
  const manifest = JSON.parse(await readFile(new URL("manifest.json", vendorRoot), "utf8"));
  const bundle = await readFile(new URL(manifest.asset, vendorRoot));
  const digest = createHash("sha256").update(bundle).digest("hex");
  assert.equal(manifest.version, SIP_JS_VERSION);
  assert.equal(manifest.version, "0.21.2");
  assert.equal(digest, manifest.sha256);
  assert.equal(manifest.license, "MIT");
  assert.ok((await readFile(new URL("LICENSE.md", vendorRoot), "utf8")).includes("MIT License"));
  assert.equal(fileURLToPath(new URL(manifest.asset, vendorRoot)).includes("node_modules"), false);
  const index = await readFile(new URL("../web/index.html", import.meta.url), "utf8");
  const scriptSources = [...index.matchAll(/<script[^>]+src="([^"]+)"/g)].map(match => match[1]);
  assert.ok(scriptSources.includes(`/assets/vendor/sip.js/${manifest.asset}`));
  assert.ok(scriptSources.every(source => source.startsWith("/assets/")));
  assert.equal(index.includes(`integrity="${manifest.sri}"`), true);
  const app = await readFile(new URL("../web/app.mjs", import.meta.url), "utf8");
  assert.match(app, /requireSipJs\(\)/);
  assert.match(app, /dataset\.sipJsVersion/);
});

test("SIP adapter accepts only injected SimpleUser and trusted WSS", () => {
  class FakeSimpleUser { constructor(server, options) { this.server = server; this.options = options; } }
  const adapter = new SipAdapter({library: {Web: {SimpleUser: FakeSimpleUser}}});
  const client = adapter.createClient("wss://lab.example:7443", {delegate: {}});
  assert.equal(client.server, "wss://lab.example:7443");
  assert.throws(() => adapter.createClient("wss://lab.example:7443", {}), /already created/);
  assert.throws(() => new SipAdapter({library: {}}), /bundle/);
  assert.throws(() => new SipAdapter({library: {Web: {SimpleUser: FakeSimpleUser}}}).createClient("ws://lab", {}), /wss/);
});

test("SIP registration keeps credentials inside the injected client options", async () => {
  const calls = [];
  class FakeSimpleUser {
    constructor(server, options) { this.server = server; this.options = options; calls.push("construct"); }
    async connect() { calls.push("connect"); }
    async register() { calls.push("register"); }
  }
  const adapter = new SipAdapter({library: {Web: {SimpleUser: FakeSimpleUser}}});
  const result = await adapter.register({
    server: "wss://lab.example:7443", domain: "lab.example", username: "1000",
    password: "demo-secret", remoteAudio: {}, audioConstraint: true,
  });
  assert.deepEqual(result, {status: "registered", username: "1000"});
  assert.deepEqual(calls, ["construct", "connect", "register"]);
  assert.equal(adapter.stateMachine.state, "registered");
  assert.equal(adapter.simpleUser.options.userAgentOptions.authorizationPassword, "demo-secret");
  assert.equal(JSON.stringify(adapter.simpleUser.options).includes("localStorage"), false);
  const source = await readFile(new URL("../web/sip_adapter.mjs", import.meta.url), "utf8");
  for (const forbidden of ["localStorage", "sessionStorage", "fetch(", "location."]) assert.equal(source.includes(forbidden), false);
  assert.equal(source.includes("console."), false);
  assert.match(source, /logBuiltinEnabled:\s*false/);
  const app = await readFile(new URL("../web/app.mjs", import.meta.url), "utf8");
  assert.match(app, /const password = ui\["sip-password"\]\.value;\s*ui\["sip-password"\]\.value = "";/);
  assert.equal(/fetch\([^)]*password/s.test(app), false);
  const index = await readFile(new URL("../web/index.html", import.meta.url), "utf8");
  assert.match(index, /id="sip-password" type="password" autocomplete="off"/);
});

test("SIP registration failure is sanitized and enters failed state", async () => {
  class FailingSimpleUser { async connect() { throw new Error("Authorization: demo-secret"); } }
  const adapter = new SipAdapter({library: {Web: {SimpleUser: FailingSimpleUser}}});
  await assert.rejects(
    adapter.register({server: "wss://lab.example:7443", domain: "lab.example", username: "1000", password: "demo-secret"}),
    error => error.message === "SIP registration failed" && !error.message.includes("demo-secret"),
  );
  assert.equal(adapter.stateMachine.state, "failed");
  assert.equal(adapter.simpleUser, undefined);
});

test("SIP adapter handles outbound audio call, answer event, and hangup", async () => {
  const calls = [];
  class FakeSimpleUser {
    constructor(server, options) { this.options = options; }
    async connect() {}
    async register() {}
    async call(target) { calls.push(["call", target]); }
    async hangup() { calls.push(["hangup"]); this.options.delegate.onCallHangup(); }
  }
  const remoteAudio = {id: "remote-audio"};
  const adapter = new SipAdapter({library: {Web: {SimpleUser: FakeSimpleUser}}});
  await adapter.register({
    server: "wss://lab.example:7443", domain: "lab.example", username: "1000",
    password: "demo-secret", remoteAudio,
  });
  assert.equal(adapter.simpleUser.options.media.remote.audio, remoteAudio);
  await adapter.call({extension: "1001", domain: "lab.example"});
  assert.equal(adapter.stateMachine.state, "ringing");
  adapter.simpleUser.options.delegate.onCallAnswered();
  assert.equal(adapter.stateMachine.state, "established");
  await adapter.hangup();
  assert.equal(adapter.stateMachine.state, "terminated");
  assert.deepEqual(calls, [["call", "sip:1001@lab.example"], ["hangup"]]);
});

test("SIP adapter attaches an established call's remote stream", async () => {
  const remoteAudio = {srcObject: null, played: false, async play() { this.played = true; }};
  class FakeSimpleUser {
    constructor(server, options) { this.options = options; }
    async connect() {}
    async register() {}
  }
  const adapter = new SipAdapter({library: {Web: {SimpleUser: FakeSimpleUser}}});
  await adapter.register({
    server: "wss://lab.example:7443",
    domain: "lab.example",
    username: "1000",
    password: "demo-secret",
    remoteAudio,
  });
  const stream = {id: "remote-audio-stream"};
  assert.deepEqual(await adapter.attachRemoteStream(stream), {status: "attached"});
  assert.equal(remoteAudio.srcObject, stream);
  assert.equal(remoteAudio.played, true);
});

test("SIP adapter sends bounded RFC 2833-compatible DTMF digits", async () => {
  const sent = [];
  class FakeSimpleUser {
    constructor(server, options) { this.options = options; }
    async connect() {}
    async register() {}
    async call() {}
    async sendDTMF(digits) { sent.push(digits); }
  }
  const adapter = new SipAdapter({library: {Web: {SimpleUser: FakeSimpleUser}}});
  await adapter.register({
    server: "wss://lab.example:7443",
    domain: "lab.example",
    username: "1000",
    password: "demo-secret",
  });
  await adapter.call({extension: "5000", domain: "lab.example"});
  adapter.simpleUser.options.delegate.onCallAnswered();
  assert.deepEqual(await adapter.sendDtmf("1#20"), {status: "sent", digits: "1#20"});
  assert.deepEqual(sent, ["1#20"]);
  await assert.rejects(adapter.sendDtmf("1;shutdown"), /invalid DTMF digits/);
});

test("SIP adapter handles incoming audio call and answer", async () => {
  class FakeSimpleUser {
    constructor(server, options) { this.options = options; }
    async connect() {}
    async register() {}
    async answer() { this.options.delegate.onCallAnswered(); }
  }
  const adapter = new SipAdapter({library: {Web: {SimpleUser: FakeSimpleUser}}});
  await adapter.register({server: "wss://lab.example:7443", domain: "lab.example", username: "1000", password: "demo-secret"});
  adapter.simpleUser.options.delegate.onCallReceived();
  assert.equal(adapter.stateMachine.state, "incoming");
  await adapter.answer();
  assert.equal(adapter.stateMachine.state, "established");
});

test("SIP state model covers all required states and rejects invalid transitions", () => {
  assert.deepEqual(SIP_STATES, ["idle", "connecting", "registering", "registered", "incoming", "ringing", "established", "terminating", "terminated", "failed"]);
  const machine = new SipStateMachine();
  const observed = [];
  machine.onChange(event => observed.push(`${event.previous}->${event.state}`));
  for (const state of ["connecting", "registering", "registered", "ringing", "established", "terminating", "terminated", "registered"]) {
    machine.transition(state);
  }
  assert.equal(machine.state, "registered");
  assert.equal(observed.length, 8);
  assert.throws(() => machine.transition("established"), /invalid SIP transition/);
  assert.throws(() => machine.transition("password=hunter2"), /unknown SIP state/);
});

test("media devices distinguish permission denial and missing input", async () => {
  const denied = new MediaDeviceController({mediaDevices: {
    getUserMedia: async () => { const error = new Error("denied"); error.name = "NotAllowedError"; throw error; },
  }});
  assert.equal((await denied.requestMicrophone()).status, "denied");

  const missing = new MediaDeviceController({mediaDevices: {
    getUserMedia: async () => ({getTracks: () => []}),
    enumerateDevices: async () => [{kind: "audiooutput", deviceId: "speaker", label: "Speaker"}],
  }});
  assert.equal((await missing.requestMicrophone()).devices.status, "missing-input");
});

test("media devices select input, output, and attach remote audio", async () => {
  const selectedSinks = [];
  const audio = {srcObject: null, setSinkId: async id => selectedSinks.push(id), play: async () => {}};
  const controller = new MediaDeviceController({
    remoteAudio: audio,
    mediaDevices: {
      getUserMedia: async () => ({getTracks: () => [{stop() {}}]}),
      enumerateDevices: async () => [
        {kind: "audioinput", deviceId: "mic", label: "Microphone"},
        {kind: "audiooutput", deviceId: "speaker", label: "Speaker"},
      ],
    },
  });
  const permission = await controller.requestMicrophone();
  assert.equal(permission.status, "granted");
  assert.deepEqual(controller.selectInput("mic"), {audio: {deviceId: {exact: "mic"}}, video: false});
  assert.equal((await controller.selectOutput("speaker")).status, "selected");
  assert.deepEqual(selectedSinks, ["speaker"]);
  const stream = {id: "remote"};
  assert.equal((await controller.attachRemoteStream(stream)).status, "attached");
  assert.equal(audio.srcObject, stream);
  assert.throws(() => controller.selectInput("unknown"), /unknown audio input/);
});

test("bounded evidence rendering drops oldest rows and reports the configured limit", () => {
  const list = {rows: [], replaceChildren(...rows) { this.rows = rows; }};
  const dropped = {textContent: "", dataset: {}};
  const panel = new BoundedPanel({limit: 3, list, dropped, renderRow: value => `row:${value}`});
  for (const value of ["one", "two", "three", "four", "five"]) panel.push(value);
  assert.deepEqual(list.rows, ["row:three", "row:four", "row:five"]);
  assert.equal(panel.droppedCount, 2);
  assert.equal(dropped.dataset.dropped, "2");
  assert.match(dropped.textContent, /已丢弃 2 条旧记录.*上限 3/);
});

test("live evidence exposes only allowlisted event fields and metric families", () => {
  const event = formatEvent({
    timestamp: "2026-08-29T12:00:00Z", type: "CHANNEL_HANGUP", correlation_id: "abcd1234",
    summary: "answered", password: "must-not-render", authorization: "must-not-render",
  });
  assert.match(event, /CHANNEL_HANGUP/);
  assert.equal(event.includes("must-not-render"), false);
  assert.deepEqual(parseMetrics([
    "# HELP freeswitch_up availability",
    "freeswitch_up 1",
    'freeswitch_calls_total{result="answered"} 4',
    'unexpected_metric{password="secret"} 9',
  ].join("\n")), ["freeswitch_up 1", "freeswitch_calls_total 4"]);
});

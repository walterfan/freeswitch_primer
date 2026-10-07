import {renderMarkdown} from "./markdown.mjs?v=15.0.1";
import {ProgressStore, validLessonId} from "./progress.mjs";
import {checkPath} from "./checks.mjs";
import {buildPreflight} from "./preflight.mjs";
import {requireSipJs, SipAdapter, SIP_JS_VERSION} from "./sip_adapter.mjs";
import {MediaDeviceController} from "./media_devices.mjs";
import {LiveEvidenceController} from "./live_evidence.mjs";
import {groupLessonsByPhase, lessonHash, selectLesson} from "./course_map.mjs";
import {diagnoseFailure} from "./diagnostics.mjs";

const ids = ["course-map","progress-count","progress-bar","lesson-reader","lesson-position","previous-lesson","next-lesson","complete-lesson","completion-note","service-status","health-components","lesson-checks","request-microphone","audio-input","audio-output","media-status","remote-audio","sip-username","sip-password","sip-register","sip-disconnect","sip-status","call-destination","call-start","call-answer","call-hangup","call-dtmf","call-send-dtmf","event-status","event-dropped","event-list","metrics-status","metrics-dropped","metrics-list"];
const ui = Object.fromEntries(ids.map(id => [id, document.getElementById(id)]));
const progress = new ProgressStore(window.localStorage);
const mediaDevices = new MediaDeviceController({remoteAudio: ui["remote-audio"]});
const liveEvidence = new LiveEvidenceController({
  eventList: ui["event-list"], eventDropped: ui["event-dropped"], eventStatus: ui["event-status"],
  metricsList: ui["metrics-list"], metricsDropped: ui["metrics-dropped"], metricsStatus: ui["metrics-status"],
});
let manifest;
let activeLesson;
let sipAdapter;
let publicSipConfig;

function updateCallControls(state) {
  const registered = ["registered", "terminated"].includes(state);
  const active = ["incoming", "ringing", "established"].includes(state);
  ui["call-start"].disabled = !registered;
  ui["call-answer"].disabled = state !== "incoming";
  ui["call-hangup"].disabled = !active;
  ui["call-send-dtmf"].disabled = state !== "established";
  ui["sip-disconnect"].disabled = !["registered", "terminated", "incoming", "ringing", "established"].includes(state);
}

function showSipFailure(error, fallbackStage) {
  const diagnostic = diagnoseFailure(error, fallbackStage);
  ui["sip-status"].textContent = `failed: ${diagnostic.detail}；${diagnostic.remediation}`;
}

function updateProgress() {
  const completed = progress.completed();
  ui["progress-count"].textContent = `${completed.size} / ${manifest.lessons.length}`;
  ui["progress-bar"].style.width = `${100 * completed.size / manifest.lessons.length}%`;
  document.querySelectorAll(".lesson-link").forEach(button => {
    const done = completed.has(button.dataset.lessonId);
    button.classList.toggle("complete", done);
    button.querySelector(".checkmark").textContent = done ? "✓" : "";
  });
  const done = activeLesson && completed.has(activeLesson.id);
  ui["complete-lesson"].textContent = done ? "撤销完成" : "标记本课完成";
  ui["completion-note"].textContent = done ? "进度仅保存在本浏览器" : "";
}

function renderMap() {
  const grouped = groupLessonsByPhase(manifest);
  ui["course-map"].replaceChildren(...manifest.phases.map(phase => {
    const section = document.createElement("section"); section.className = "phase";
    const heading = document.createElement("h2"); heading.textContent = `${phase.title_zh} · ${phase.days[0]}–${phase.days[1]} 天`;
    const outcome = document.createElement("p"); outcome.className = "phase-outcome";
    outcome.textContent = phase.outcome_zh || "";
    section.append(heading, outcome, ...grouped.get(phase.id).map(lesson => {
      const button = document.createElement("button");
      button.type = "button"; button.className = "lesson-link"; button.dataset.lessonId = lesson.id;
      button.innerHTML = `<span class="day">${String(lesson.day).padStart(2,"0")}</span><span class="title"></span><span class="checkmark"></span>`;
      button.querySelector(".title").textContent = lesson.title_zh;
      button.addEventListener("click", () => { location.hash = lessonHash(lesson.id); });
      return button;
    }));
    return section;
  }));
}

function renderChecks(lesson) {
  ui["lesson-checks"].replaceChildren(...lesson.checks.map(check => {
    const wrapper = document.createElement("div"); const button = document.createElement("button");
    button.type = "button"; button.textContent = `${check.id} · ${check.mode}`; button.disabled = check.mode === "manual";
    const result = document.createElement("p"); result.className = "health-detail";
    if (check.mode === "manual") result.textContent = "人工验收：按课程步骤记录听感或端到端证据。";
    else button.addEventListener("click", async () => {
      button.disabled = true; result.textContent = "检查中…";
      try {
        const response = await fetch(checkPath(activeLesson, check.id), {method: "POST"});
        const payload = await response.json();
        result.className = `check-result ${payload.status || "fail"}`;
        result.textContent = `${payload.status}: ${payload.evidence || "无证据"}${payload.remediation ? `；${payload.remediation}` : ""}`;
      } catch (error) { result.className = "check-result unavailable"; result.textContent = `unavailable: ${error.message}`; }
      finally { button.disabled = false; }
    });
    wrapper.append(button, result); return wrapper;
  }));
}

async function openLesson(lessonId) {
  const selected = selectLesson(manifest, lessonId);
  activeLesson = selected;
  document.querySelectorAll(".lesson-link").forEach(button => button.classList.toggle("active", button.dataset.lessonId === selected.id));
  ui["lesson-position"].textContent = `第 ${selected.day} 天 · ${selected.title_zh}`;
  ui["previous-lesson"].disabled = selected.day === 1; ui["next-lesson"].disabled = selected.day === manifest.lessons.length;
  renderChecks(selected);
  try {
    const response = await fetch(`/api/v1/lessons/zh-CN/${selected.id}`);
    if (!response.ok) throw new Error(`课程内容尚未就绪（HTTP ${response.status}）`);
    ui["lesson-reader"].innerHTML = renderMarkdown(await response.text());
  } catch (error) {
    ui["lesson-reader"].innerHTML = `<div class="error-card"><h2>${selected.title_zh}</h2><p>${error.message}</p><p>课程地图仍可使用；请运行内容校验查看实施进度。</p></div>`;
  }
  updateProgress();
}

async function loadHealth() {
  try {
    const response = await fetch("/api/v1/health", {cache: "no-store"}); const health = await response.json();
    ui["service-status"].textContent = `教学服务：${health.status}`;
    ui["service-status"].style.color = health.status === "healthy" ? "var(--accent)" : "var(--warning)";
    let publicConfig = {};
    try {
      publicConfig = await (await fetch("/api/v1/public-config", {cache: "no-store"})).json();
      publicSipConfig = publicConfig.sip;
      ui["sip-register"].disabled = !publicSipConfig?.wss_url || !publicSipConfig?.domain;
      ui["sip-status"].textContent = publicSipConfig ? `idle: ${publicSipConfig.wss_url}` : "unavailable: WSS 配置缺失";
    } catch (_) {}
    const stages = buildPreflight(health.components, {
      secureContext: window.isSecureContext,
      mediaDevices: Boolean(navigator.mediaDevices?.getUserMedia),
      wssUrl: publicConfig.sip?.wss_url,
    });
    ui["health-components"].replaceChildren(...stages.map(item => {
      const name = item.name;
      const row = document.createElement("div"); row.className = "health-item";
      row.innerHTML = `<span class="health-dot ${item.status}"></span><div><strong></strong><div class="health-detail"></div></div>`;
      row.querySelector("strong").textContent = `${name} · ${item.status}`;
      row.querySelector(".health-detail").textContent = item.status === "healthy" ? item.detail : `${item.detail}；${item.remediation}`;
      return row;
    }));
  } catch (_) { ui["service-status"].textContent = "教学服务：不可用（课程仍可阅读）"; }
}

function replaceDeviceOptions(select, devices, fallback) {
  const options = devices.map((device, index) => {
    const option = document.createElement("option");
    option.value = device.deviceId;
    option.textContent = device.label || `${fallback} ${index + 1}`;
    return option;
  });
  select.replaceChildren(...options);
  select.disabled = options.length === 0;
}

ui["request-microphone"].addEventListener("click", async () => {
  ui["request-microphone"].disabled = true;
  ui["media-status"].textContent = "正在请求麦克风权限…";
  const result = await mediaDevices.requestMicrophone();
  ui["media-status"].textContent = `${result.status}: ${result.detail}`;
  if (result.devices) {
    replaceDeviceOptions(ui["audio-input"], result.devices.inputs, "麦克风");
    replaceDeviceOptions(ui["audio-output"], result.devices.outputs, "扬声器");
  }
  ui["request-microphone"].disabled = false;
});

ui["audio-input"].addEventListener("change", () => {
  try {
    mediaDevices.selectInput(ui["audio-input"].value);
    ui["media-status"].textContent = "selected: 输入设备已选择，将用于下一通呼叫";
  } catch (error) { ui["media-status"].textContent = `error: ${error.message}`; }
});

ui["audio-output"].addEventListener("change", async () => {
  try {
    const result = await mediaDevices.selectOutput(ui["audio-output"].value);
    ui["media-status"].textContent = `${result.status}: ${result.detail}`;
  } catch (error) { ui["media-status"].textContent = `error: ${error.message}`; }
});

ui["sip-register"].addEventListener("click", async () => {
  if (!publicSipConfig || !sipAdapter) return;
  const username = ui["sip-username"].value.trim();
  const password = ui["sip-password"].value;
  ui["sip-password"].value = "";
  ui["sip-register"].disabled = true;
  try {
    await sipAdapter.register({
      server: publicSipConfig.wss_url,
      domain: publicSipConfig.domain,
      username,
      password,
      remoteAudio: ui["remote-audio"],
      audioConstraint: mediaDevices.inputDeviceId ? {deviceId: {exact: mediaDevices.inputDeviceId}} : true,
    });
    ui["sip-disconnect"].disabled = false;
  } catch (_) {
    ui["sip-register"].disabled = false;
    showSipFailure(_, "sip_authentication");
  }
});

ui["sip-disconnect"].addEventListener("click", async () => {
  ui["sip-disconnect"].disabled = true;
  await sipAdapter?.disconnect();
  ui["sip-register"].disabled = !publicSipConfig;
});

ui["call-start"].addEventListener("click", async () => {
  ui["call-start"].disabled = true;
  try {
    await sipAdapter?.call({extension: ui["call-destination"].value.trim(), domain: publicSipConfig?.domain});
  } catch (error) {
    showSipFailure(error, "wss");
    updateCallControls(sipAdapter?.stateMachine.state || "failed");
  }
});

ui["call-answer"].addEventListener("click", async () => {
  ui["call-answer"].disabled = true;
  try {
    await sipAdapter?.answer();
  } catch (error) {
    showSipFailure(error, "sdp");
    updateCallControls(sipAdapter?.stateMachine.state || "failed");
  }
});

ui["call-hangup"].addEventListener("click", async () => {
  ui["call-hangup"].disabled = true;
  try {
    await sipAdapter?.hangup();
  } catch (error) {
    showSipFailure(error, "wss");
    updateCallControls(sipAdapter?.stateMachine.state || "failed");
  }
});

ui["call-send-dtmf"].addEventListener("click", async () => {
  const digits = ui["call-dtmf"].value.trim();
  ui["call-send-dtmf"].disabled = true;
  try {
    const result = await sipAdapter?.sendDtmf(digits);
    ui["sip-status"].textContent = `${result.status}: DTMF ${result.digits} 已发送`;
  } catch (error) {
    ui["sip-status"].textContent = `failed: ${error.message}`;
  } finally {
    updateCallControls(sipAdapter?.stateMachine.state || "failed");
  }
});

async function start() {
  requireSipJs();
  document.documentElement.dataset.sipJsVersion = SIP_JS_VERSION;
  sipAdapter = new SipAdapter();
  sipAdapter.stateMachine.onChange(event => {
    ui["sip-status"].textContent = `${event.state}: ${event.detail}`;
    ui["sip-status"].dataset.state = event.state;
    updateCallControls(event.state);
  });
  const response = await fetch("/content/manifest.json", {cache: "no-cache"});
  if (!response.ok) throw new Error(`manifest 加载失败（HTTP ${response.status}）`);
  manifest = await response.json(); renderMap(); updateProgress();
  const requested = location.hash.slice(1); await openLesson(validLessonId(requested) ? requested : "day-01"); await loadHealth(); await liveEvidence.start();
}

window.addEventListener("hashchange", () => openLesson(location.hash.slice(1)));
ui["previous-lesson"].addEventListener("click", () => { location.hash = `day-${String(activeLesson.day - 1).padStart(2,"0")}`; });
ui["next-lesson"].addEventListener("click", () => { location.hash = `day-${String(activeLesson.day + 1).padStart(2,"0")}`; });
ui["complete-lesson"].addEventListener("click", () => { const completed = progress.completed(); progress.setCompleted(activeLesson.id, !completed.has(activeLesson.id)); updateProgress(); });
start().catch(error => { ui["lesson-reader"].innerHTML = `<div class="error-card"><h2>网站初始化失败</h2><p>${error.message}</p></div>`; });

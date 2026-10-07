const DIAGNOSTICS = Object.freeze({
  secure_context: ["安全上下文", "使用 localhost 或可信 HTTPS 打开网站，不要关闭浏览器安全检查。"],
  certificate: ["证书信任", "确认网站和 FreeSWITCH WSS 证书已被当前浏览器信任，且 SAN 匹配访问地址。"],
  wss: ["WSS 传输", "确认使用 wss:// 地址、端口可达，并检查 Sofia WSS profile 的监听状态。"],
  sip_authentication: ["SIP 认证", "确认教程分机和密码来自隔离实验目录；检查 REGISTER 的 401/200 过程，不要分享密码。"],
  sdp: ["SDP 协商", "检查 offer/answer 的音频方向、端口和编解码交集；分享抓包前先脱敏。"],
  ice: ["ICE 连通性", "检查候选地址和 RTP 可达性；远端环境需要可信地址或按实验文档配置 NAT。"],
  dtls_srtp: ["DTLS-SRTP", "确认 WebRTC 指纹交换完成，并检查浏览器与 FreeSWITCH 的 DTLS-SRTP 状态。"],
  codec: ["编解码", "确认双方有共同的音频 codec；不要仅凭信令成功判断媒体已建立。"],
  no_audio: ["无音频", "确认麦克风轨道、远端 audio 元素、输出设备和浏览器播放权限，再检查 RTP 证据。"],
});

const STAGES = Object.freeze([
  ["secure_context", /secure context|isSecureContext|securityerror/i],
  ["certificate", /certificate|trust|err_cert|\btls\b/i],
  ["sip_authentication", /authorization|authentication|digest|401|403|register/i],
  ["sdp", /\bsdp\b|session description|offer|answer/i],
  ["ice", /\bice\b|candidate/i],
  ["dtls_srtp", /\bdtls\b|\bsrtp\b/i],
  ["codec", /codec|payload type|opus|pcmu|pcma|g722/i],
  ["no_audio", /no[- ]audio|remote audio|media track|muted|playback/i],
  ["wss", /\bwss?\b|websocket|transport|connection/i],
]);

function failureText(failure) {
  if (typeof failure === "string") return failure;
  if (!failure || typeof failure !== "object") return "";
  const error = failure.error;
  return [
    failure.stage,
    failure.code,
    failure.name,
    failure.message,
    error?.name,
    error?.message,
  ].filter(value => typeof value === "string").join(" ");
}

export function diagnoseFailure(failure, fallbackStage = "wss") {
  const explicitStage = typeof failure?.stage === "string" ? failure.stage : "";
  const detectedStage = STAGES.find(([, pattern]) => pattern.test(failureText(failure)))?.[0];
  const stage = DIAGNOSTICS[explicitStage] ? explicitStage : (detectedStage || fallbackStage);
  const [label, remediation] = DIAGNOSTICS[stage] || DIAGNOSTICS.wss;
  return {
    stage,
    status: "failed",
    detail: `${label}阶段失败`,
    remediation,
  };
}

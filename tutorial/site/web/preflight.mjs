const remedies = {
  service: "启动教学服务并刷新页面；正文可继续离线阅读。",
  freeswitch: "确认 FreeSWITCH 容器正在运行，再执行 fs_cli -x status。",
  esl: "检查 ESL 是否启用、端口可达，以及密码环境变量是否正确。",
  heartbeat: "检查 ESL event 连接和 HEARTBEAT 订阅，等待下一个心跳周期。",
  sip_profile: "检查 Sofia profile 是否已启动，以及 WSS binding 是否存在。",
  secure_context: "使用 localhost 或可信 HTTPS 打开网站；不要绕过证书校验。",
  microphone: "允许麦克风权限并确认系统存在可用的音频输入设备。",
  wss: "使用可信证书的 wss:// 地址，并确认浏览器能够建立 WebSocket。",
};

export function buildPreflight(components = {}, browser = {}) {
  const output = [];
  for (const name of ["service", "freeswitch", "esl", "heartbeat", "sip_profile"]) {
    const item = components[name] || {status: "unavailable", detail: "未收到状态"};
    output.push({name, status: item.status, detail: item.detail || "", remediation: item.remediation || remedies[name]});
  }
  output.push({name: "secure_context", status: browser.secureContext ? "healthy" : "unavailable", detail: browser.secureContext ? "浏览器处于安全上下文" : "非安全上下文无法可靠使用麦克风", remediation: remedies.secure_context});
  output.push({name: "microphone", status: browser.mediaDevices ? "healthy" : "unavailable", detail: browser.mediaDevices ? "浏览器支持音频设备 API；权限在呼叫前确认" : "navigator.mediaDevices 不可用", remediation: remedies.microphone});
  const secureWss = typeof browser.wssUrl === "string" && browser.wssUrl.startsWith("wss://");
  output.push({name: "wss", status: secureWss ? "healthy" : "unavailable", detail: secureWss ? `目标 ${browser.wssUrl}` : "未配置安全 WSS URL", remediation: remedies.wss});
  return output;
}

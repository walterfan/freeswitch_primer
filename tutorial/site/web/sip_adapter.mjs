import {SipStateMachine} from "./sip_state.mjs";

export const SIP_JS_VERSION = "0.21.2";

export function requireSipJs(library = globalThis.SIP) {
  if (!library?.Web?.SimpleUser) {
    throw new Error("本地 SIP.js bundle 未加载；请检查 vendor 文件及其完整性");
  }
  return library;
}

// The application depends on this narrow boundary, not directly on SIP.js.
// A fake library can therefore exercise state and signaling behavior in Node tests.
export class SipAdapter {
  constructor({library = globalThis.SIP, stateMachine = new SipStateMachine()} = {}) {
    this.library = requireSipJs(library);
    this.stateMachine = stateMachine;
    this.simpleUser = undefined;
    this.remoteAudio = undefined;
  }

  createClient(server, options) {
    if (this.simpleUser) throw new Error("SIP client already created");
    if (typeof server !== "string" || !server.startsWith("wss://")) {
      throw new Error("SIP transport must use a trusted wss:// URL");
    }
    this.simpleUser = new this.library.Web.SimpleUser(server, options);
    return this.simpleUser;
  }

  async register({server, domain, username, password, remoteAudio, audioConstraint = true}) {
    if (!/^[A-Za-z0-9_.-]{1,64}$/.test(username || "")) throw new Error("invalid SIP username");
    if (!/^[A-Za-z0-9.-]{1,253}$/.test(domain || "")) throw new Error("invalid SIP domain");
    if (typeof password !== "string" || !password.length || password.length > 256) {
      throw new Error("SIP password is required");
    }
    this.stateMachine.transition("connecting", "正在建立 WSS 连接");
    this.remoteAudio = remoteAudio;
    try {
      const client = this.createClient(server, {
        aor: `sip:${username}@${domain}`,
        delegate: {
          onCallReceived: () => this.handleIncomingCall(),
          onCallAnswered: () => this.handleCallAnswered(),
          onCallHangup: () => this.handleCallHangup(),
        },
        media: {
          constraints: {audio: audioConstraint, video: false},
          remote: {audio: remoteAudio},
        },
        reconnectionAttempts: 3,
        reconnectionDelay: 4,
        userAgentOptions: {
          authorizationUsername: username,
          authorizationPassword: password,
          logBuiltinEnabled: false,
        },
      });
      await client.connect();
      this.stateMachine.transition("registering", "WSS 已连接，正在进行 SIP Digest 注册");
      await client.register();
      this.stateMachine.transition("registered", `分机 ${username} 已注册`);
      return {status: "registered", username};
    } catch (_) {
      try { await this.simpleUser?.disconnect?.(); } catch (_) {}
      this.simpleUser = undefined;
      this.remoteAudio = undefined;
      this.stateMachine.transition("failed", "WSS 或 SIP 注册失败");
      throw new Error("SIP registration failed");
    }
  }

  handleIncomingCall() {
    if (this.stateMachine.state === "registered") {
      this.stateMachine.transition("incoming", "收到音频来电");
    }
  }

  handleCallAnswered() {
    if (["incoming", "ringing"].includes(this.stateMachine.state)) {
      this.stateMachine.transition("established", "音频通话已建立");
    }
  }

  async attachRemoteStream(stream) {
    if (!this.remoteAudio || !("srcObject" in this.remoteAudio)) {
      throw new Error("remote audio element is unavailable");
    }
    this.remoteAudio.srcObject = stream;
    await this.remoteAudio.play?.();
    return {status: "attached"};
  }

  handleCallHangup() {
    if (["incoming", "ringing", "established", "terminating"].includes(this.stateMachine.state)) {
      this.stateMachine.transition("terminated", "音频通话已结束");
    }
  }

  async call({extension, domain}) {
    if (!this.simpleUser) throw new Error("SIP client is not registered");
    if (!/^[0-9*#]{1,32}$/.test(extension || "")) throw new Error("invalid destination");
    if (!/^[A-Za-z0-9.-]{1,253}$/.test(domain || "")) throw new Error("invalid SIP domain");
    if (this.stateMachine.state === "terminated") this.stateMachine.transition("registered", "准备下一通呼叫");
    if (this.stateMachine.state !== "registered") throw new Error("SIP client is busy");
    this.stateMachine.transition("ringing", `正在呼叫分机 ${extension}`);
    try {
      await this.simpleUser.call(`sip:${extension}@${domain}`);
    } catch (_) {
      this.stateMachine.transition("failed", "呼叫建立失败");
      throw new Error("SIP call failed");
    }
  }

  async answer() {
    if (!this.simpleUser || this.stateMachine.state !== "incoming") throw new Error("no incoming call");
    try {
      await this.simpleUser.answer();
      if (this.stateMachine.state === "incoming") this.handleCallAnswered();
    } catch (_) {
      this.stateMachine.transition("failed", "接听失败");
      throw new Error("SIP answer failed");
    }
  }

  async hangup() {
    if (!this.simpleUser || !["incoming", "ringing", "established"].includes(this.stateMachine.state)) {
      throw new Error("no active call");
    }
    this.stateMachine.transition("terminating", "正在挂断音频通话");
    try {
      await this.simpleUser.hangup();
      if (this.stateMachine.state === "terminating") this.handleCallHangup();
    } catch (_) {
      this.stateMachine.transition("failed", "挂断失败");
      throw new Error("SIP hangup failed");
    }
  }

  async sendDtmf(digits) {
    if (!this.simpleUser || this.stateMachine.state !== "established") {
      throw new Error("no established call");
    }
    if (typeof digits !== "string" || !/^[0-9*#]{1,32}$/.test(digits)) {
      throw new Error("invalid DTMF digits");
    }
    if (typeof this.simpleUser.sendDTMF !== "function") {
      throw new Error("SIP client does not support RFC 2833 DTMF");
    }
    try {
      await this.simpleUser.sendDTMF(digits);
      return {status: "sent", digits};
    } catch (_) {
      throw new Error("SIP DTMF failed");
    }
  }

  async disconnect() {
    if (!this.simpleUser) return;
    if (["incoming", "ringing", "established"].includes(this.stateMachine.state)) {
      await this.hangup();
    }
    if (this.stateMachine.state === "terminated") {
      this.stateMachine.transition("registered", "通话已结束，准备注销");
    }
    if (!["failed", "terminating"].includes(this.stateMachine.state)) {
      this.stateMachine.transition("terminating", "正在注销 SIP 用户");
    }
    try { await this.simpleUser.unregister?.(); } catch (_) {}
    try { await this.simpleUser.disconnect?.(); } finally {
      this.simpleUser = undefined;
      this.remoteAudio = undefined;
      if (this.stateMachine.state !== "failed") this.stateMachine.transition("terminated", "SIP 连接已关闭");
    }
  }
}

function message(error) {
  return error instanceof Error ? error.message : String(error);
}

export class MediaDeviceController {
  constructor({mediaDevices = globalThis.navigator?.mediaDevices, remoteAudio} = {}) {
    this.mediaDevices = mediaDevices;
    this.remoteAudio = remoteAudio;
    this.inputs = [];
    this.outputs = [];
    this.inputDeviceId = "";
    this.outputDeviceId = "";
  }

  async requestMicrophone() {
    if (!this.mediaDevices?.getUserMedia) {
      return {status: "unavailable", detail: "浏览器不支持 getUserMedia"};
    }
    try {
      const stream = await this.mediaDevices.getUserMedia({audio: true, video: false});
      for (const track of stream.getTracks?.() || []) track.stop();
      const devices = await this.refreshDevices();
      return {status: "granted", detail: "麦克风权限已授予", devices};
    } catch (error) {
      if (error?.name === "NotAllowedError" || error?.name === "SecurityError") {
        return {status: "denied", detail: "麦克风权限被拒绝；请在浏览器站点设置中允许后重试"};
      }
      if (error?.name === "NotFoundError") {
        return {status: "missing-input", detail: "未找到音频输入设备"};
      }
      return {status: "error", detail: `麦克风初始化失败：${message(error)}`};
    }
  }

  async refreshDevices() {
    if (!this.mediaDevices?.enumerateDevices) {
      return {status: "unavailable", detail: "浏览器不支持设备枚举", inputs: [], outputs: []};
    }
    const devices = await this.mediaDevices.enumerateDevices();
    this.inputs = devices.filter(device => device.kind === "audioinput");
    this.outputs = devices.filter(device => device.kind === "audiooutput");
    if (!this.inputs.length) {
      return {status: "missing-input", detail: "未找到音频输入设备", inputs: [], outputs: this.outputs};
    }
    return {
      status: this.outputs.length ? "ready" : "missing-output",
      detail: this.outputs.length ? "音频设备已就绪" : "未枚举到独立输出设备，将使用系统默认输出",
      inputs: this.inputs,
      outputs: this.outputs,
    };
  }

  selectInput(deviceId) {
    if (!this.inputs.some(device => device.deviceId === deviceId)) throw new Error("unknown audio input device");
    this.inputDeviceId = deviceId;
    return {audio: {deviceId: {exact: deviceId}}, video: false};
  }

  async selectOutput(deviceId) {
    if (!this.outputs.some(device => device.deviceId === deviceId)) throw new Error("unknown audio output device");
    if (typeof this.remoteAudio?.setSinkId !== "function") {
      return {status: "unsupported", detail: "当前浏览器不支持网页选择输出设备，请使用系统声音设置"};
    }
    await this.remoteAudio.setSinkId(deviceId);
    this.outputDeviceId = deviceId;
    return {status: "selected", detail: "输出设备已切换"};
  }

  async attachRemoteStream(stream) {
    if (!this.remoteAudio || !("srcObject" in this.remoteAudio)) {
      return {status: "unavailable", detail: "远端音频元素不可用"};
    }
    this.remoteAudio.srcObject = stream;
    try {
      await this.remoteAudio.play?.();
      return {status: "attached", detail: "远端音频已连接"};
    } catch (error) {
      return {status: "blocked", detail: `远端音频等待用户播放：${message(error)}`};
    }
  }
}

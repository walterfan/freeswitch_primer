# 第 16 天：Playback、Record 与 Phrase

## 今日成果

- 用 `tone_stream` 在不依赖产品声音包的情况下证明 playback 通路。
- 分清：文件存在、playback 执行、人耳听到，是三件不同的事。
- 知道教程 `sound_root` 与 vanilla `sounds/` 必须隔离。

## 核心原理

`playback` 把文件或 tone 写到 channel。`record` 把媒体写到实验目录。`phrase` 通过语言宏组合提示。教学网站检查 `tutorial/labs/day-16/sounds/` 下三份 wav；缺失时检查结果是 **unavailable**，并给出安装提示，不会假装播放成功。

教程 IVR 默认使用 `tone_stream://%(1000,0,350,440)`，因此第 18 天即使没有 wav 也能做 DTMF。本课若要听真实语音，需实验室批准的 wav，采样率通常 8000 或 16000、单声道。

## 源码导航

- [`tutorial/labs/day-16/README.md`](../../../labs/day-16/README.md)
- [`tutorial/site/config/config.yaml`](../../../site/config/config.yaml)：`sound_root`
- [`src/mod/applications/mod_dptools/mod_dptools.c`](../../../../src/mod/applications/mod_dptools/mod_dptools.c)：`playback` / `read`
- [`conf/vanilla/dialplan/default.xml`](../../../../conf/vanilla/dialplan/default.xml)：`9197` milliwatt、`9196` echo
- [本课实验目录](../../../labs/day-16/)

## 源码深挖

拨号计划里的 `playback` 不是直接打开文件，而是 `mod_dptools.c:playback_function` 调用 `switch_ivr_play_file`。后者建立 file handle、按文件格式读帧、把帧写进 session 的 media path，并通过 input callback 处理 DTMF。`record` 则由 `record_function` 进入 `switch_ivr_record_file`，参数中的 limit、采样率和静音阈值会影响结束条件。

`phrase` 属于更高一层的提示组合：它可以把宏、语言和多个音频片段交给播放路径。文件不存在时，应用会把结果写入 channel response；这与客户端“听不到”不同，后者还可能是 RTP 或 codec 问题。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "playback_function|record_function|phrase_function|switch_ivr_play_file|switch_ivr_record_file|SWITCH_ADD_APP" \
  "$FREESWITCH_SRC/src/mod/applications/mod_dptools/mod_dptools.c" \
  "$FREESWITCH_SRC/src/switch_ivr_play_say.c"
```

先用 `tone_stream://` 验证 application 和媒体输出，再用 WAV 验证 file interface；这样可以把“文件格式/路径”与“RTP 不通”拆开。

## 引导实验

前置条件：1000 可注册（SIP 或浏览器）。

1. 看检查约定：

   ```bash
   ls -l tutorial/labs/day-16/sounds/
   curl -fsS -X POST http://127.0.0.1:7009/api/v1/checks/day-16/ivr-sounds
   ```

   缺文件时记录 unavailable 与 remediation，不要去 `conf/vanilla` 复制私有资产。

2. 不依赖 wav 的通路证明：1000 拨 `9197`，应听到 1004 Hz 测试音。这是 playback/tone 平面。

3. 可选：将实验室 wav 放到 `sounds/playback.wav` 后，用 originate 播放（在容器内路径以实际挂载为准；未挂载则跳过并标 unavailable）：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x "originate {ignore_early_media=true}loopback/9197/default &park"
   ```

   听到测试音后 `hupall`。**不要**把录音写到 `$HOME` 或 vanilla `recordings_dir` 后提交 git。

4. 若做 record：指定容器内 `/tmp/tutorial-day16.wav`，实验结束删除。禁止录音文件离开隔离主机。

清理：删除 `/tmp/tutorial-*.wav`。不要修改 vanilla sounds。

## 独立挑战

说明为什么“三个 wav 的 stat 成功”不能证明采样率、回放设备和听感。给出你会额外做的两个检查。

## 验收

**验收方式：半自动加人工。**

- **pass**：9197 可听；sound 检查的 pass/unavailable 与磁盘一致。
- **fail**：缺文件却把检查标 pass。
- **unavailable**：无声音包且 9197 也因媒体失败（先按第 9 天排障）。

## 故障排查

- 现象：检查 unavailable → 证据：缺 wav → 原因：仓库故意不带音频 → 恢复：按 lab README 放入批准文件或继续用 tone。
- 现象：文件在、playback 报 not found → 证据：容器路径与 sound_root 不同 → 原因：网站与 FreeSWITCH 文件系统视角分裂（第 2 天）→ 恢复：确认谁实际读文件。
- 现象：录音泄漏 → 证据：wav 出现在共享目录 → 原因：用了默认 recordings_dir → 恢复：删除文件并改 /tmp。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 播放 | playback | 向 channel 输出音频资源 |
| 音调流 | tone_stream | 用参数生成提示音，不依赖 wav |
| 短语宏 | phrase macro | 按语言组织的提示组合 |
| 声音根目录 | sound_root | 教学检查使用的隔离音频目录 |

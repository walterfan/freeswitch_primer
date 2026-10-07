# 第 10 天：NAT、地址宣告、抓包与单向音频

## 今日成果

- 能把单向音频定位到“信令成功、RTP 单向或双向丢失”，而不是重装软电话。
- 能读懂 internal profile 的 `rtp-ip` / `ext-rtp-ip` 与当前 `local_ip_v4`。
- 知道何时该抓 UDP/RTP，以及抓包为什么不能带进公开 issue。

## 核心原理

SIP 200 只交换 SDP 里的地址和端口。真正的声音走 RTP。UA 或 FreeSWITCH 若把私网地址写进 SDP，对端会把媒体发到不可达地址，表现为单向或完全无声。vanilla 用 `rtp-ip=$${local_ip_v4}` 和 `ext-rtp-ip=$${external_rtp_ip}` 做宣告；本 Docker lab 的 SIP IP 已验证为 `10.100.212.8`。若 UA 在另一网段或经过 NAT，还要看对称 RTP、防火墙 UDP 范围。

端口分层：

- TCP/UDP 5060：SIP。通了只证明信令。
- UDP RTP 范围：媒体。不通就是无声。
- 7443：WSS，与本课 SIP 软电话无关。

## 源码导航

- [`conf/vanilla/sip_profiles/internal.xml`](../../../../conf/vanilla/sip_profiles/internal.xml)：`rtp-ip`、`ext-rtp-ip`。
- [`conf/vanilla/vars.xml`](../../../../conf/vanilla/vars.xml)：`local_ip_v4`、`external_rtp_ip`。
- [`src/switch_nat.c`](../../../../src/switch_nat.c)：NAT 辅助。
- [`src/switch_rtp.c`](../../../../src/switch_rtp.c)
- [`docker/README.md`](../../../../docker/README.md)：容器网络约束。
- [本课实验目录](../../../labs/day-10/)

## 源码深挖

RTP 地址问题通常不是一个开关。`switch_rtp_set_local_address` 决定本地 bind，`switch_rtp_set_remote_address` 决定发送目标；core media 还会根据 SDP、ICE、AUTOADJ 和 profile 的外部地址决定宣告什么。NAT 下最危险的组合是：SIP 信令地址可达，但 SDP 的 `c=`/`m=` 地址或 RTP 端口不可达。

FreeSWITCH 的自动调整只能依据实际收到的包修正部分远端信息，不能替代正确的 `ext-rtp-ip`、端口映射和防火墙。单向音频应先判断是“对端没发包”“FS 收到但没发”“对端收到但播放失败”，而不是直接改 codec。

```bash
export FREESWITCH_SRC="${FREESWITCH_SRC:-$HOME/workspace/rtc/freeswitch}"
rg -n "switch_rtp_set_local_address|switch_rtp_set_remote_address|SWITCH_RTP_FLAG_AUTOADJ|ext-rtp-ip|switch_determine_ice_type" \
  "$FREESWITCH_SRC/src/switch_rtp.c" "$FREESWITCH_SRC/src/switch_core_media.c" \
  "$FREESWITCH_SRC/src/mod/endpoints/mod_sofia"
```

每次只改一层并重测：先固定宣告地址，再确认 UDP 端口，再用抓包验证方向。`rtp_autoflush`、codec、DTMF 等参数不能修复错误的 NAT 路由。

## 引导实验

前置条件：第 9 天 9196 在同一局域网应能回声。本课制造或观察“跨网段/错误宣告”差异。

1. 读取宣告地址，不要把它们当监控标签公开扩散：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'global_getvar local_ip_v4'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x 'global_getvar external_rtp_ip'
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'sofia status profile internal' | grep -E 'SIP IP|EXT-SIP|RTP'
   ```

   记录：内部 RTP 宣告是否等于容器可达 IP。

2. 同一台机器上的两个 UA 打 9196，确认双向回声作为基线。

3. 若有第二网段 UA（例如手机 LTE），再打 9196 或 1001。记录：信令是否 200、是否单向。不要为了“打通”而关闭防火墙或把 ESL 暴露到公网。

4. 只在隔离实验网抓**极短** RTP 证据。优先看计数而不是保存 pcap：

   ```bash
   docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
     -x 'sofia status profile internal'
   ```

   通话中 `uuid_dump` 看 `rtp_use_ssrc` / codec；若容器有 `tcpdump` 才允许：

   ```bash
   docker exec freeswitch sh -lc 'command -v tcpdump && echo yes || echo no'
   ```

   没有 tcpdump 则标该步 unavailable。有的话只抓几十个包：`udp and not port 5060`，实验结束删除 pcap。

5. 对照清单（每项 yes/no）：SDP 地址是否为对端可达 IP；本端麦克风静音；本端听筒选错设备；RTP 被防火墙丢弃。第 11 天浏览器问题不要在本课用 SIP UA 结论代替。

清理：删除 pcap。恢复 UA 网络。

## 独立挑战

写一份给下一值班同学的单向音频 runbook，顺序必须是：设备静音 → echo 9196 → codec 摘要 → 宣告 IP 是否可达 → 再考虑抓包。禁止第一步就 `sofia loglevel 9`。

## 验收

**验收方式：手动加命令。**

- **pass**：能解释本 lab 的 RTP 宣告 IP；有基线 echo；单向场景有分层结论。
- **fail**：信令成功却把原因写成“UUID 错误”。
- **unavailable**：无法制造第二网络路径，且无 tcpdump。

## 故障排查

- 现象：局域网 echo 好、跨 NAT 单向 → 证据：SIP 200、一侧无 RTP → 原因：SDP 私网地址或对称 NAT → 恢复：修正 ext-rtp-ip / 使用可达媒体地址，不关 TLS。
- 现象：双方都无声但 9197 测试音本端听得到 → 证据：播放路径好、bridge RTP 失败 → 原因：对端 RTP 未到 → 恢复：查对端防火墙。
- 现象：抓包含完整号码与 SDP → 证据：pcap 未脱敏 → 原因：把诊断文件当报告附件 → 恢复：删除文件，只保留计数。

## 中英术语表

| 中文 | English | 本课含义 |
|---|---|---|
| 地址宣告 | address advertisement | SDP 中填写的媒体 IP/端口 |
| 外部 RTP 地址 | ext-rtp-ip | 对公网/对端宣告的 RTP 地址 |
| 单向音频 | one-way audio | 只有一侧的 RTP 到达 |
| 对称 NAT | symmetric NAT | 映射随目标变化，导致 RTP 回程失败 |

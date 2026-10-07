# Appendix-01: FAQ

<!-- maintained-by: human+ai -->

Answers below come from [Quick Start](../0.getting-started/02-quick-start.md), [Architecture](../1.architecture/01-architecture.md), [Build](../4.development/02-build.md), [Data and API](../1.architecture/04-data-and-api.md), [Observability](../5.operations/02-observability.md), the [Users Manual](https://developer.signalwire.com/freeswitch/), and `README.md`. Vanilla XML is a demo PBX, not a production baseline.

## Frequently Asked Questions

### Should I install from source or from packages?

`README.md` recommends **packages** for operators. That path is FSGET plus `freeswitch-meta-all` (`scripts/packaging/README.md`). FSGET needs a SignalWire Personal Access Token; without it, `apt` / `docker/master` returns **401**.

This clone is the **source** path: install Sofia-SIP / libks / SpanDSP / signalwire-c first, then `./bootstrap.sh -j && ./configure --prefix="$HOME/fs" --disable-fhs && make && make install`. `release` tracks packaged releases; `prerelease` tracks `master`.

### Why are Sofia-SIP, SpanDSP, and libks missing from this git tree?

They are **out-of-tree**. CI clones [sofia-sip](https://github.com/freeswitch/sofia-sip); `docker/examples/Debian11/Dockerfile` also builds libks, SpanDSP, and signalwire-c. macOS CI uses Homebrew `signalwire/homebrew-signalwire/{libks2,signalwire-c2,spandsp}`. `configure` / link fails until those libraries are installed. They are not under `libs/` (APR, SRTP, ESL, VPX *are*). Sofia-SIP layout, NUA/NTA usage, and how `mod_sofia` uses `nua`/`tport`: [Tech Stack](../1.architecture/02-tech-stack.md#sofia-sip-library), [Architecture](../1.architecture/01-architecture.md#sofia-sip-out-of-tree).

### What is the difference between endpoint, channel, session, and call?

An **endpoint** is a protocol module (`mod_sofia`, `mod_verto`). A **channel** is one leg (`switch_channel_t`). A **session** is the runtime container around that channel (`switch_core_session_t`). A **call** is one or more associated channels (typically two legs joined by a **bridge**). Directory / dialplan / configuration are three separate XML domains. Numbered definitions: [Overview — FS core concepts](../0.getting-started/01-overview.md#fs-core-concepts).

### Why are there two files named `modules.conf`?

| File | When | Role |
|------|------|------|
| `build/modules.conf.in` → `modules.conf` | Build | Which `src/mod/...` trees `bootstrap.sh` / Autotools compile |
| `conf/vanilla/autoload_configs/modules.conf.xml` | Runtime | Which compiled DSOs `switch_loadable_module_init()` loads |

`bootstrap.sh` copies `modules.conf.in` → `modules.conf` if missing. Uncomment a `src/mod/...` line **after** bootstrap and **before** `./configure` / `make`. A module can be compiled and still stay unloaded if it is absent from the XML list.

### How is `<load module="mod_sofia"/>` parsed?

It is not a separate parser. Vanilla `freeswitch.xml` includes `autoload_configs/*.xml` into the `configuration` section. After `switch_xml_init()`, `switch_loadable_module_init()` calls `switch_xml_open_cfg("modules.conf")`, walks each `<load>` child, and `dlopen`s `{mod_dir}/mod_sofia.so`. The exported table `mod_sofia_module_interface` supplies `mod_sofia_load`. `reloadxml` does not re-run that walk. Detail: [Architecture](../1.architecture/01-architecture.md#runtime-load-tag), [Workflows](../1.architecture/05-workflows.md#runtime-load-tag). What the DSO registers after load: [Loadable module contract](../1.architecture/01-architecture.md#loadable-module-contract). Dispatch by name: [Interface registry lookup](../1.architecture/05-workflows.md#interface-registry-lookup).

### How do I load `mod_xml_rpc` and serve a portal page?

`mod_xml_rpc` must be enabled in the build-time `modules.conf` as
`xml_int/mod_xml_rpc` and in the runtime
`autoload_configs/modules.conf.xml` with
`<load module="mod_xml_rpc"/>`. Its settings are in
`autoload_configs/xml_rpc.conf.xml`; the default HTTP port is **8080**.
The module exposes XML-RPC at `/RPC2`, API wrappers at `/api/`,
`/webapi/`, `/txtapi/`, and `/xmlapi/`, and serves static files from
`htdocs_dir`.

It is an HTTP server and static-file mechanism, not a complete administration
UI. Put a portal entry page at `<htdocs_dir>/portal/index.html`; requesting
`/portal` redirects there. The bundled starter assets are under
`htdocs/portal/`. Set all three `auth-realm`, `auth-user`, and `auth-pass`
parameters to protect non-`/pub` requests with Basic Authentication. The
sample credentials are for demonstration only; use a strong password and
restrict port 8080 with a firewall, VPN, or HTTPS reverse proxy.

### Why is there no IDL for modules?

Modules and core share `switch.h` in one process. Load registers **name + function pointer** into hashes (`application_hash`, `api_hash`, `endpoint_hash`, …). Dialplan `application="bridge"` and `api status` are lookups, not RPC stubs. [Loadable module contract](../1.architecture/01-architecture.md#loadable-module-contract).

### What is the default prefix, and how do I start FreeSWITCH?

Default prefix without `--prefix` is `/usr/local/freeswitch` (`configure.ac`).
The PKB developer path uses `--prefix="$HOME/fs" --disable-fhs` so the
installed files stay below that prefix. A custom prefix without
`--disable-fhs` uses FHS paths.

```bash
"$HOME/fs/bin/freeswitch" -ncwait -nonat
```

`-ncwait`: background, no console, wait until the core is ready (`src/switch.c`). `-nonat`: skip UPnP/NAT-PMP pinholes — required unless you *want* auto-NAT (`conf/vanilla/README_IMPORTANT.txt`). Foreground: `freeswitch -c -nonat`. Stop: `freeswitch -stop`. Debian systemd uses `/usr/bin/freeswitch -u freeswitch -g freeswitch -ncwait -nonat`.

### How do I prove FreeSWITCH is up?

```bash
"$HOME/fs/bin/fs_cli" -x status
```

Working output starts with `UP` (same check as `docker/base_image/healthcheck.sh`, regex `^UP`). Then `fs_cli -x sofia status` for SIP profiles. If `fs_cli` cannot connect, the switch may be down, `mod_event_socket` may not be loaded, the client target may not reach the listener, **or the source IP failed the Event Socket ACL**. `fs_cli` defaults to `127.0.0.1:8021`; vanilla listens on `::`:8021. Loopback works while the host LAN IP is rejected until you set `apply-inbound-acl` — [Remote Event Socket ACL](../5.operations/01-runbook.md#remote-event-socket-acl).

First call without a second phone: register as `1000` / password `1234` on port 5060, then dial **`9196`** (echo). See [Quick Start](../0.getting-started/02-quick-start.md) and [Users Manual Chapter 2](https://developer.signalwire.com/freeswitch/foundations/getting-started).

### How do I track a SIP call from the log?

Correlate with the **channel UUID**, not the SIP `Call-ID`. Core logs attach the UUID via `SWITCH_CHANNEL_SESSION_LOG` (`mod_logfile` prefixes it when `uuid=true`). Channel events use header **`Unique-ID`**. A-leg and B-leg each have their own UUID; `show channels` lists `uuid` per leg and **`call_uuid`** to join them (`show calls` uses `b_uuid` on the paired view). SIP `Call-ID` is a signaling identifier — it appears in siptrace, not as the file-logger prefix. Step-by-step grep / `/uuid` / `bridge_uuid` / CDR: [Runbook — Follow one call by channel UUID](../5.operations/01-runbook.md#follow-one-call-by-channel-uuid). Column meanings and a captured two-leg `bridge`: [Workflows — show channels during a bridge](../1.architecture/05-workflows.md#worked-example-show-channels-during-a-bridge).

Vanilla console is **`info`** (`$${console_loglevel}`). For one inbound call:

1. Note the time, then `fs_cli -x 'show channels'` while the call is up (or `show calls` after). Copy the UUID (truncate in notes if you will share the log). A custom dialplan `log` line (for example `hit test_1005 uuid=…`) is another way to recover the UUID after hangup.
2. Follow `$log_dir/freeswitch.log` (`$prefix/log/freeswitch.log` on a prefix install). On the source Docker image (`-nf`): `docker logs -f --since=10s freeswitch`, or `docker exec … tail -F /usr/local/freeswitch/log/freeswitch.log`.
3. `sofia profile internal siptrace on` dumps **tport** bytes (Sofia-SIP), before `sofia_event_callback`. Packet-to-session hop: [Workflows](../1.architecture/05-workflows.md#packet-path-udptcp-to-nua). Those lines often omit the channel UUID — also grep `uuid_getvar <uuid> sip_call_id`.
4. Filter: `fs_cli` `/uuid <channel-uuid>` (clear with `/uuid`), or grep the file logger for that UUID. Typical order: Sofia `recv` INVITE → `Processing … in context …` → `EXECUTE` apps → `Hangup … [CS_EXECUTE] [<cause>]`.

A **407** then a second **INVITE** is Digest (`Proxy-Authenticate`); a **401** then a second **REGISTER** is the same math with `WWW-Authenticate`. Neither challenge is by itself a failure. `EXECUTE … ivr(demo_ivr)` plus `mod_sndfile` “No such file or directory” under `sounds/en/us/callie/ivr/` means missing Callie prompts (`make sounds-install`), not a SIP routing miss. Redact `Proxy-Authorization`, nonces, full UUIDs, and `Call-ID` before sharing. Full command list: [Observability](../5.operations/02-observability.md).

### How do I read `show channels`?

`fs_cli -x 'show channels'` dumps the core SQL `channels` scoreboard as CSV: one row per **leg**, not per call. Join legs with **`call_uuid`**. `uuid` is the session id for logs and `uuid_*` APIs. `state` is `CS_*`; `callstate` is `CCS_*` (`ACTIVE`, `RINGING`, …). Directory user (`presence_id`, `dest` on the A-leg) can differ from the SIP Contact user in `name` / `callee_num` on WebRTC (often `user@….invalid`).

Worked example (1002 inbound `bridge` to 1001’s WebRTC Contact, opus/DTLS-SRTP): [Workflows — show channels during a bridge](../1.architecture/05-workflows.md#worked-example-show-channels-during-a-bridge). Schema: `create_channels_sql` in `src/switch_core_sqldb.c`. Operator command: [Runbook](../5.operations/01-runbook.md#verification-commands).

### Why does REGISTER get 401 then 200 OK?

Vanilla internal requires Digest. First REGISTER has no `Authorization` → Sofia **401** + `WWW-Authenticate` (`sofia_reg_auth_challenge`, nonce in `sip_authentication`). The UAC retries with `CSeq` incremented and `Authorization`. Match → **200 OK** and a `sip_registrations` row; `AUTH_FORBIDDEN` → **403**; stale nonce → **401** `stale=true`. INVITE on the same profile uses **407** + `Proxy-Authenticate`. Message-level walkthrough: [Workflows — REGISTER Digest after 401](../1.architecture/05-workflows.md#worked-example-register-digest-after-401). Directory password is `$${default_password}` in vanilla (`1234`) — demo only.

### Is vanilla XML a production config?

No. `conf/vanilla` is a **demo PBX** so you can test immediately (`conf/vanilla/README_IMPORTANT.txt`). Directory users `1000`–`1019` share **`default_password=1234`** (`conf/vanilla/vars.xml`). Do not publish 5060/8021 without ACLs. Before any public bind: change `default_password` (or `scripts/perl/randomize-passwords.pl`), change the ESL password, and start with `-nonat` unless NAT helpers are intentional.

### What is the default Event Socket port and password?

Password **`ClueCon`**, port **8021**. Distinguish three files:

- Vanilla / testing runtime (`conf/vanilla/autoload_configs/event_socket.conf.xml`, `conf/testing/autoload_configs/event_socket.conf.xml`) listen on **`::`** (all interfaces) with `apply-inbound-acl` **commented out**.
- Module sample (`src/mod/event_handlers/mod_event_socket/conf/autoload_configs/event_socket.conf.xml`) binds **`127.0.0.1`**.
- `fs_cli` client default is **`127.0.0.1:8021`** (`libs/esl/fs_cli.c`, `libs/esl/fs_cli.conf`).

Commenting out `apply-inbound-acl` is **not** “no ACL”. `mod_event_socket` `config()` still defaults to **`loopback.auto`** (`127.0.0.0/8` and `::1/128`). Change the password and set an explicit ACL before any non-loopback exposure. Wrong password closes the socket (`-ERR invalid`) **after** the ACL check.

### Why does `fs_cli` to the LAN IP fail with `Error Connecting []` while `127.0.0.1` works?

The Event Socket **is** listening on 8021. SIP `rtp-ip` / `sip-ip` are unrelated. The failure is ESL ACL.

TCP opens, then Event Socket sends `Content-Type: text/rude-rejection` and `Access Denied, go away.` — that is what `fs_cli` surfaces as `Error Connecting []`. Connecting to the host LAN address (for example `10.100.212.8:8021`) uses that source IP, not `127.0.0.1`, so `loopback.auto` denies it. A remote laptop is denied for the same reason.

Allow loopback **and** the client CIDR on one list (`esl_allowed` in `acl.conf.xml`), set `<param name="apply-inbound-acl" value="esl_allowed"/>` in `event_socket.conf.xml`, then `reloadacl` and `reload mod_event_socket` over **loopback**. Verify with `fs_cli -H <lan-ip> -P 8021` on the FreeSWITCH box first. Full XML and commands: [Remote Event Socket ACL](../5.operations/01-runbook.md#remote-event-socket-acl).

Do not open ESL with `0.0.0.0/0`. ESL is full control of FreeSWITCH; do not leave `ClueCon` if 8021 is reachable beyond a trusted LAN.

### Why does Docker SIP or RTP fail unless I use host networking?

Runtime containers expect **`--network host`** (`docker/README.md`). Typical
ports: SIP 5060/5080, TLS 5061/5081, Sofia SIP WebSocket 5066/7443, Verto
WS/WSS 8081/8082, ESL **8021**, RTP UDP 16384–32768 and 64535–65535.
Publishing those UDP ranges through a user-defined bridge is the usual failure
mode. Packaged images (`docker/master/Dockerfile`) still need a SignalWire
`TOKEN` build-arg. The token-free Debian 11 source-image example
(`docker/examples/Debian11/Dockerfile`) clones FreeSWITCH and its dependencies
during the image build; it does not build this checkout.

### Why is WebRTC media silent over a VPN even though SIP signaling works?

Inspect the SDP answer returned by FreeSWITCH and check the `a=candidate`
lines. If they contain the server's public address instead of its reachable
VPN address (for example, `10.100.x.x`), SIP signaling may succeed while RTP
media cannot reach the browser. Set `ext-rtp-ip` in the Sofia profile used by
the call to the VPN address:

```xml
<param name="rtp-ip" value="10.100.x.x"/>
<param name="ext-rtp-ip" value="10.100.x.x"/>
```

`rtp-ip` selects the local RTP interface and `ext-rtp-ip` is the address
advertised in SDP/ICE. Reload the XML and restart the affected profile, then
verify the new SDP answer and confirm that the VPN permits the configured RTP
UDP port range. Do not change a profile used by public clients to a VPN-only
address unless those clients also have a route to the VPN.

### How do I build on Windows vs Unix?

Unix / macOS: Autotools — `./bootstrap.sh -j && ./configure --prefix=... --disable-fhs && make && make install`. Windows: `Freeswitch.2017.sln` / `w32/`, typically `msbuild Freeswitch.2017.sln -t:build -verbosity:minimal -property:Configuration=Release -property:Platform=x64` (`msbuild.cmd` locates VS via `vswhere.exe`). CI uploads `x64\*.msi` on `master` / `v1.10` / `v1.11`. Historical `src/CMakeLists.txt` is **not** the Unix CI path.

### Where are the prompt / sound files?

They are **not** in this git tree. `make sounds` / `make sounds-install` downloads Callie 8 kHz packages (`Makefile.am`, default `en-us-callie-8000`). Specs at repo root: `freeswitch-sounds-*.spec`. Windows sound packages live in a separate [freeswitch-sounds](https://github.com/freeswitch/freeswitch-sounds) release repo (`README.md`).

### What is the difference between `$${var}` and `${var}`?

`$${name}` is a **preprocessor** variable, expanded once while assembling XML (`vars.xml`, `#set` / `X-PRE-PROCESS cmd="set"`). `${name}` is a **channel** variable, expanded at call time. Mixing them is a common config bug. [Users Manual Chapter 3](https://developer.signalwire.com/freeswitch/configuration/xml). Print them from `fs_cli` with the next question.

### How do I print variables with fs_cli?

`mod_commands` registers these APIs (`src/mod/applications/mod_commands/mod_commands.c`). Globals live on the core (`switch_core_dump_variables()` / `switch_core_get_variable_dup()` in `src/switch_core.c`). Channel vars need a live session UUID (`show channels`).

| Goal | Command |
|------|---------|
| All globals (`name=value`) | `global_getvar` |
| One global | `global_getvar <var>` |
| Expand `$${name}` | `eval $${name}` |
| All channel vars | `uuid_dump <uuid>` |
| One channel var | `uuid_getvar <uuid> <var>` |
| Expand `${name}` on a call | `eval uuid:<uuid> ${name}` |

```bash
fs_cli -x global_getvar
fs_cli -x "global_getvar domain"
fs_cli -x 'eval $${domain}'
fs_cli -x "show channels"
fs_cli -x "uuid_dump <uuid>"
fs_cli -x "uuid_dump <uuid> json"
fs_cli -x "uuid_getvar <uuid> sip_from_user"
fs_cli -x 'eval uuid:<uuid> ${sip_from_user}'
```

`uuid_dump` format defaults to `txt`; also `json` and `xml`. A missing channel name prints `_undef_`. Interactive: `fs_cli`, then the same APIs (`help global_getvar`, `help uuid_dump`, `help uuid_getvar`, `help eval`). Other inspect commands: [Runbook](../5.operations/01-runbook.md#verification-commands).

### Why does my internal SIP profile say `context=public` but registered phones use the `default` dialplan?

Vanilla `sip_profiles/internal.xml` sets `context=public` for **unauthenticated** inbound. After digest auth, the directory user’s `user_context` (vanilla `default`) wins. [Chapter 1](https://developer.signalwire.com/freeswitch/foundations/introduction).

### How can I add a special FreeSWITCH number that plays a video to a video call?

最简单的方案是复用 `mod_av`，让拨号计划用标准 `playback` 播放 MP4，不需要写 C 模块。

在实际运行目录的 `$${conf_dir}/dialplan/default/` 下新增 `10_video_demo.xml`：

```xml
<include>
  <extension name="video_demo_9527">
    <condition field="destination_number" expression="^9527$">
      <action application="answer"/>
      <action application="playback"
              data="$${sounds_dir}/video/demo.mp4"/>
    </condition>
  </extension>
</include>
```

然后：

```bash
fs_cli -x "module_exists mod_av"
fs_cli -x "reloadxml"
```

第一条应返回 `true`。把视频放到 `$${sounds_dir}/video/demo.mp4`，并确保 FreeSWITCH 进程有读取权限。

客户端发起视频呼叫并拨 9527，SIP SDP 必须带 `m=video`；纯音频呼叫不会凭空建立视频流。

当前仓库已经默认编译并加载 `mod_av`：

- `modules.conf` (line 1)
- `modules.conf.xml` (line 89)
- 默认支持 H264/VP8：`vars.xml` (line 244)

仓库还有现成的 FSV 测试号码：`9193` 录像、`9194` 播放，见 `default.xml` (line 697)。

注意把号码放在认证用户使用的 `default` context，不要直接暴露到 `public` context；文件路径应固定，不能直接取自 SIP Header 等外部输入。遇到只有声音、没有画面时，优先检查呼叫是否协商出 H264/VP8 视频流。

### Where is this Project Knowledge Base?

**`man/`** at the repo root — not `docs/man/` (Unix man pages). Sphinx: `cd man && poetry install && make html-en`. Operator configuration is the [FreeSWITCH Users Manual](https://developer.signalwire.com/freeswitch/). Historical tutorials remain on [FreeSWITCH Explained](https://developer.signalwire.com/freeswitch/FreeSWITCH-Explained/) and [Confluence](https://freeswitch.org/confluence/). The PKB maps those concepts onto files in this clone; it does not copy parameter tables.

### Why does FreeSWITCH C look unlike typical C (`malloc` / `pthread` / BSD sockets)?

It is a **lifetime-centric runtime** on top of FSPR, not a thin syscall wrapper. Allocate from a memory pool (`switch_core_alloc`, `switch_core_session_alloc`); destroy the pool at hangup or module shutdown. Threads, mutexes, queues, and sockets are `switch_*` in `src/include/switch_apr.h`. Read [C Runtime Framework](../1.architecture/06-c-runtime.md) before `mod_sofia.c`.

### Why does this tree say `1.11.3-dev` when CI also builds `v1.10` / `v1.11`?

This workspace is **1.11.3-dev** (`AC_INIT` in `configure.ac`; keep in sync with `SWITCH_VERSION_*` and `build/next-release.txt`). GitHub Actions run on `master`, `v1.10`, and `v1.11`. Packaged `build.yml`: those version branches → `release`; `master` → **unstable**. FSGET `prerelease` tracks what you see on `master`. Verify a running binary with `freeswitch -version` / `fs_cli -x version`.

## Related Documentation

- [Quick Start](../0.getting-started/02-quick-start.md)
- [Architecture](../1.architecture/01-architecture.md)
- [C Runtime Framework](../1.architecture/06-c-runtime.md)
- [Build](../4.development/02-build.md)
- [Runbook](../5.operations/01-runbook.md)
- [Observability](../5.operations/02-observability.md)
- [Documentation Process](../5.operations/03-document.md)
- [Glossary](03-glossary.md)
- [Users Manual](https://developer.signalwire.com/freeswitch/)

---
<!-- PKB-metadata
last_updated: 2026-09-18
commit: e6d261c069
updated_by: human+ai
review_status: pending
review_score: 0
reviewed_by:
confidentiality: L1
-->

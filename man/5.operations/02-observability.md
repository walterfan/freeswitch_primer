# 11. Observability

<!-- maintained-by: human+ai -->

What operators look at first: `fs_cli -x status`, `$log_dir/freeswitch.log`, then Sofia/channel listings. This tree has **no** `mod_prometheus` and no in-repo Grafana/Prometheus dashboards. Counters live in `fs_cli` / ESL events; optional SNMP is compiled off by default.

Upstream operator docs: [FreeSWITCH Users Manual](https://developer.signalwire.com/freeswitch/) (Part 11 Troubleshooting). Historical wiki: [FreeSWITCH Explained](https://developer.signalwire.com/freeswitch/FreeSWITCH-Explained/).

## Philosophy

FreeSWITCH is a long-running media switch. Runtime visibility is **session-centric**, not request-trace-centric:

1. Is the process up and accepting sessions? (`status`, HEARTBEAT events)
2. What is in the log for this **session UUID**?
3. What SIP/media path is live? (`sofia status`, `show channels`, siptrace)
4. What happened after hangup? (CDR files / event socket)

Vanilla config (`conf/vanilla/`) is a demo PBX. Treat its log levels as a starting point, not a production SLA.

## Logging

Core logger: `src/switch_log.c`, API in `src/include/switch_log.h`. Levels are `switch_log_level_t` (`src/include/switch_types.h`); names from `switch_log_level2str` / `LEVELS[]` in `switch_log.c`.

| Numeric | `switch_log_level_t` | `switch_log_level2str` |
|---------|----------------------|------------------------|
| `-1` | `SWITCH_LOG_DISABLE` | `DISABLE` |
| `0` | `SWITCH_LOG_CONSOLE` | `CONSOLE` |
| `1` | `SWITCH_LOG_ALERT` | `ALERT` |
| `2` | `SWITCH_LOG_CRIT` | `CRIT` |
| `3` | `SWITCH_LOG_ERROR` | `ERR` |
| `4` | `SWITCH_LOG_WARNING` | `WARNING` |
| `5` | `SWITCH_LOG_NOTICE` | `NOTICE` |
| `6` | `SWITCH_LOG_INFO` | `INFO` |
| `7` | `SWITCH_LOG_DEBUG` | `DEBUG` |
| `101`–`110` | `SWITCH_LOG_DEBUG1` … `DEBUG10` | clamped to `DEBUG` by `switch_log_level2str` |

Vanilla knobs (three different filters — do not conflate them):

| Knob | File | Default | Effect |
|------|------|---------|--------|
| Global hard level | `conf/vanilla/autoload_configs/switch.conf.xml` `loglevel` | `debug` | `SCSC_LOGLEVEL` / `fsctl loglevel`; caps what any logger can emit |
| Console module | `conf/vanilla/autoload_configs/console.conf.xml` `loglevel` | `$${console_loglevel}` → **`info`** (`vars.xml`) | What `fs_cli` / stdout shows |
| File module mappings | `conf/vanilla/autoload_configs/logfile.conf.xml` | `all` → `console,debug,info,notice,warning,err,crit,alert` | What goes to `freeswitch.log` |
| CLI shortcuts | `switch.conf.xml` keys 7 / 8 | `console loglevel 0` / `7` | Quiet vs debug on the console only |

Line format (`switch_log_vprintf`, `SWITCH_FUNC_IN_LOG` is **not** defined in this tree):

```text
YYYY-MM-DD HH:MM:SS.uuuuuu idleCPU% [LEVEL] file:line message
```

`mod_logfile` prefixes the session UUID when `uuid=true` (vanilla default).

### Where the file lives

`mod_logfile` default path (`src/mod/loggers/mod_logfile/mod_logfile.c`): `$SWITCH_GLOBAL_dirs.log_dir/freeswitch.log`. `log_dir` is a preprocessor variable (`conf/vanilla/vars.xml`); compile-time `SWITCH_LOG_DIR` from `configure.ac`:

| Install | Log directory |
|---------|---------------|
| Prefix (`./configure --prefix=$HOME/fs --disable-fhs`) | `$prefix/log` → `$HOME/fs/log/freeswitch.log` |
| `--enable-fhs` | `${localstatedir}/log/freeswitch` |
| `--with-logfiledir=DIR` | `DIR` |
| Debian package (`debian/freeswitch.postinst`, systemd unit) | `/var/log/freeswitch` |

Vanilla `logfile.conf.xml` leaves `<param name="logfile" value="/var/log/freeswitch.log"/>` **commented**; the computed `log_dir` path is what actually opens.

Rotation: `rollover` 1048576000 bytes, `maximum-rotate` 32, `rotate-on-hup`
true. The Debian unit
(`debian/freeswitch-systemd.freeswitch.service`) does not define
`ExecReload`; use `fs_cli -x "fsctl send_sighup"` or an explicit `kill -HUP`.
The separate template `build/freeswitch.service` does define
`ExecReload=/usr/bin/kill -HUP $MAINPID`. Either HUP path fires
`SWITCH_EVENT_TRAP` with `Trapped-Signal=HUP`, allowing `mod_logfile` to
rotate.

### systemd journal vs the log file

Debian `debian/freeswitch-systemd.freeswitch.service` is `Type=forking` with `-ncwait`. It does **not** set `StandardOutput=journal`. `journalctl -u freeswitch` shows unit start/stop (and any leftover stdout of the forking parent), **not** the full switch log.

`mod_syslog` is **compiled** (`build/modules.conf.in`: `loggers/mod_syslog`) but **not loaded** in vanilla (`modules.conf.xml` comments it out). If you load it, `conf/vanilla/autoload_configs/syslog.conf.xml` sends facility `user`, ident `freeswitch`, default level `warning`, with UUIDs. On systemd hosts that often appears in the journal via the syslog socket.

`mod_graylog2` exists under `src/mod/loggers/mod_graylog2/` but is commented in both `modules.conf.in` and vanilla `modules.conf.xml`.

### Runtime verbosity

```bash
fs_cli                          # ESL; on connect sends ESL `log` at the CLI profile level
# inside fs_cli:
/log debug                      # ESL log stream to this CLI (not the file)
/nolog
/uuid <session-uuid>            # filter CLI log stream to one call
/logfilter <string>
console loglevel info           # mod_console filter (0–7 or name)
fsctl loglevel debug            # global hard level
sofia loglevel all 9            # Sofia-SIP library 0–9 (mod_sofia)
sofia tracelevel debug          # Sofia messages into switch_log
sofia profile internal siptrace on
```

`fs_cli -l debug` / `-x` one-shots: `libs/esl/fs_cli.c`. Function keys 7/8 default to `/log console` and `/log debug`.

Loaded loggers in vanilla `modules.conf.xml`: `mod_console`, `mod_logfile`. Optional JSON console lines: `console json on` (`switch_log_node_to_json`); off by default.

### Docker: follow a call

Commands below assume the container name `freeswitch` and the source-image prefix `/usr/local/freeswitch` from [Quick Start](../0.getting-started/02-quick-start.md) (`docker/examples/Debian11`, `freeswitch:local`, started with `-nf -nonat`). Substitute the name and paths if your Compose or image differs.

Vanilla `$${console_loglevel}` is **`info`**. An inbound call can succeed while `docker logs` looks quiet. Raise verbosity for one reproduction, then restore it. The source image runs **foreground** (`-nf`), so `mod_console` output is Docker stdout.

**1. Stream stdout** while you place the call:

```bash
docker logs -f --since=10s freeswitch
```

Recent lines only: `docker logs --tail 100 freeswitch`.

**2. Raise the console filter** (0–7 or a level name) if you need dialplan `EXECUTE` lines, state-machine chatter, or `mod_sndfile` warnings:

```bash
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
  -x "console loglevel debug"
```

When finished:

```bash
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
  -x "console loglevel notice"
```

`console loglevel` is the `mod_console` filter only. `fsctl loglevel` is the global cap. `/log debug` inside `fs_cli` is the ESL stream to **that** CLI, not the file logger.

**3. Interactive ESL** (filter to one leg after `show channels`):

```bash
docker exec -it freeswitch /usr/local/freeswitch/bin/fs_cli
```

Inside the CLI: `/log debug`, then `/uuid <session-uuid>`. `/nolog` stops the stream.

**4. SIP message dump** on the profile under test. Reproduce **one** registration or call, then disable tracing immediately:

```bash
docker exec freeswitch \
  /usr/local/freeswitch/bin/fs_cli \
  -x "sofia profile internal siptrace on"

docker logs -f --since=10s freeswitch

# After reproducing:
docker exec freeswitch \
  /usr/local/freeswitch/bin/fs_cli \
  -x "sofia profile internal siptrace off"
docker exec freeswitch \
  /usr/local/freeswitch/bin/fs_cli \
  -x "console loglevel notice"
```

Use `sofia global siptrace on` only when all SIP profiles are required. It is much noisier. A normal digest-authenticated call commonly contains an initial `407`, an authenticated retry, `100 Trying`, and `200 OK`; the `407` is expected challenge/response behavior, not necessarily a call failure.

**5. File logger** if stdout is empty but the switch is up. Prefix install path:

```bash
docker exec -it freeswitch \
  tail -F /usr/local/freeswitch/log/freeswitch.log
```

Debian/FHS images typically use `/var/log/freeswitch/freeswitch.log`. The slim `safarov/freeswitch` layout uses `/usr/share/freeswitch/` and `/etc/freeswitch/`; do not assume `/usr/local/freeswitch` there.

During a live call, `docker exec … fs_cli -x 'show channels'` and `sofia status profile internal` confirm legs and the SIP profile without dumping packets.

#### Reading common call-path lines

`EXECUTE [depth=0] sofia/internal/1000@<domain> ivr(demo_ivr)` means the dialplan reached the vanilla demo IVR application. That is not a SIP failure.

`[WARNING] mod_sndfile.c:… Error Opening File [/usr/local/freeswitch/sounds/en/us/callie/ivr/….wav] [… No such file or directory]` means the **Callie prompt pack is not installed**. Sound archives are not in this git tree (`Makefile.am`: `make sounds-install` downloads `en-us-callie-8000`). On the source Docker image:

```bash
docker exec -u 0 -it freeswitch bash -lc \
  'apt-get update && apt-get install -y --no-install-recommends curl'

docker exec -u 0 -it freeswitch bash -lc \
  'export DOWNLOAD_CMD="$(command -v curl) -fL -O" &&
   cd /usr/src/freeswitch &&
   make sounds-install'
```

The source image may omit `curl`, which `build/getsounds.sh` needs. Files appear on disk immediately (no restart required for the next playback). They survive `docker restart` and are lost if the container is removed. For all Callie sample rates use `make cd-sounds-install`; for Music on Hold (`9664`) use `make moh-install` / `make cd-moh-install` as in [Quick Start](../0.getting-started/02-quick-start.md). If `/usr/src/freeswitch` is missing, unpack the GitHub release tarball (`build/sounds_version.txt`, currently `en-us-callie` **1.0.53**) into `$prefix/sounds`. The `SOUND_TYPES` / `SOUND_RATES` entrypoint in `docker/base_image/` writes under `/usr/share/freeswitch/sounds`, which is not the prefix path above.

Confirm:

```bash
docker exec freeswitch ls \
  /usr/local/freeswitch/sounds/en/us/callie/ivr/ivr-welcome_to_freeswitch.wav
```

Representative redacted SIP/console snippet for a call from Zoiper user `1000` to MoH `9664`:

```text
recv <bytes> from udp/[<CLIENT_IP>]:<CLIENT_PORT>:
INVITE sip:9664@<SERVER_IP>;transport=UDP SIP/2.0
Via: SIP/2.0/UDP <CLIENT_IP>:<CLIENT_PORT>;branch=<BRANCH>;rport
From: <sip:1000@<SERVER_IP>>;tag=<FROM_TAG>
To: <sip:9664@<SERVER_IP>>
Call-ID: <CALL_ID>
CSeq: 1 INVITE
Content-Type: application/sdp

v=0
c=IN IP4 <CLIENT_IP>
m=audio <CLIENT_RTP_PORT> RTP/AVP <CODECS>
a=sendrecv

send <bytes> to udp/[<CLIENT_IP>]:<CLIENT_PORT>:
SIP/2.0 407 Proxy Authentication Required
Proxy-Authenticate: Digest realm="<SERVER_IP>", nonce="<REDACTED>"

recv <bytes> from udp/[<CLIENT_IP>]:<CLIENT_PORT>:
INVITE sip:9664@<SERVER_IP>;transport=UDP SIP/2.0
Proxy-Authorization: Digest username="1000", ... <REDACTED>

[INFO] sofia.c:<LINE> receiving invite from <CLIENT_IP>:<CLIENT_PORT>
[INFO] mod_dialplan_xml.c:<LINE> Processing 1000 <1000>->9664 in context default
send <bytes> to udp/[<CLIENT_IP>]:<CLIENT_PORT>:
SIP/2.0 100 Trying

[NOTICE] mod_dptools.c:<LINE> Channel [sofia/internal/1000@<SERVER_IP>] has been answered
send <bytes> to udp/[<CLIENT_IP>]:<CLIENT_PORT>:
SIP/2.0 200 OK
Content-Type: application/sdp

v=0
c=IN IP4 <SERVER_ADVERTISED_IP>
m=audio <SERVER_RTP_PORT> RTP/AVP 9 101
a=rtpmap:9 G722/8000
a=rtpmap:101 telephone-event/8000

EXECUTE [depth=0] sofia/internal/1000@<SERVER_IP> playback(local_stream://moh)
```

Redact `Proxy-Authorization`, digest nonces, call IDs, session UUIDs, phone numbers, and IP addresses before sharing traces. Turn tracing off after capture to avoid log volume and unnecessary credential-related data retention.

## Metrics

**No first-party metrics backend in this repo.** There is no `src/mod/**/mod_prometheus*`, no OpenTelemetry/Zipkin/Jaeger module, and no Grafana dashboard files.

What you can scrape or poll instead:

### `fs_cli -x status` (`mod_commands` `status_function`)

Printed every time you ask:

- uptime
- version and ready / not ready
- sessions since startup
- current sessions, peak, last-5-min peak
- sessions per second (last / max / peak / last-5-min peak)
- `max-sessions` (vanilla `1000` in `switch.conf.xml`)
- min idle CPU / current idle CPU
- stack size (when available)

`sessions-per-second` vanilla cap is `30`. `min-idle-cpu` is commented out.

Same numbers are fired on **HEARTBEAT** every 20s by default (`runtime.event_heartbeat_interval` in `src/switch_core.c`; XML `event-heartbeat-interval` is commented in `switch.conf.xml`): `Session-Count`, `Max-Sessions`, `Session-Per-Sec*`, `Session-Peak-*`, `Idle-CPU`, `Up-Time`.

### Optional SNMP (`mod_snmp`)

Directory exists: `src/mod/event_handlers/mod_snmp/` plus `FREESWITCH-MIB` (enterprise `27880`). **Commented** in `build/modules.conf.in` and vanilla `modules.conf.xml`. MIB gauges overlap `status`: uptime, current/max sessions, sessions per second, current calls, core UUID.

### CDRs (post-call counters, not live metrics)

| Module | In `modules.conf.in` | Loaded in vanilla | Default sink |
|--------|----------------------|-------------------|--------------|
| `mod_cdr_csv` | yes | **yes** | `$log_dir/cdr-csv/Master.csv` |
| `mod_cdr_sqlite` | yes | no | core SQLite (see `cdr_sqlite.conf.xml`) |
| `mod_json_cdr` | **commented** | no | HTTP and/or disk (`json_cdr.conf.xml`) |
| `mod_cdr_pg_csv` | commented | no | PostgreSQL; spool `$${log_dir}/cdr-pg-csv` |

Vanilla CDR template includes `${uuid}`, hangup cause, codecs, stamps (`conf/vanilla` / module sample `cdr_csv.conf.xml`).

## Tracing or Correlation

There is **no distributed trace exporter**. Correlation is the **channel UUID**, not the SIP `Call-ID`. Operator procedure (live `/uuid`, file grep, A/B-leg, CDR): [Runbook — Follow one call by channel UUID](01-runbook.md#follow-one-call-by-channel-uuid).

- Generated in `switch_core_session` request (`src/switch_core_session.c`): `switch_uuid_get` + `switch_uuid_format` into `session->uuid_str`, unless a caller supplied `use_uuid`. Channel vars `uuid` and `call_uuid` are set to the same string at create. Optional `uuid-version` `4` or `7` in `switch.conf.xml` (commented; default compiled behavior).
- Accessor: `switch_core_session_get_uuid()`.
- Log macros: `SWITCH_CHANNEL_SESSION_LOG(session)` / `SWITCH_CHANNEL_UUID_LOG(uuid)` (`src/include/switch_types.h`). File logger prefixes that UUID when `uuid=true` (vanilla `logfile.conf.xml`).
- Channel events (`switch_channel.c`) carry header **`Unique-ID`**. Bridges add `Bridge-A-Unique-ID` / `Bridge-B-Unique-ID`. Partner channel vars: `bridge_uuid`, `signal_bond`. SIP identifier: `sip_call_id`.
- Process identity: `core_uuid` (`switch_core.c` at startup). Not a call id.
- `fs_cli` `/uuid <uuid>` filters the ESL log stream to one channel. `/uuid` with no argument clears the filter. `console uuid on` prefixes UUID on `mod_console` lines without filtering.
- A bridged call has **two** channel UUIDs. Join with `call_uuid` on `show channels`, or `uuid_getvar <uuid> bridge_uuid`. Siptrace lines often lack the channel UUID — grep `sip_call_id` as well.

Subscribe to events over ESL (`mod_event_socket`, vanilla listen `::` port **8021**, password `ClueCon` in `event_socket.conf.xml` — change that before any network exposure). `fs_cli /event plain all` is the interactive equivalent.

## Alerts and Health Signals

This repo does **not** ship alert rules, PagerDuty/Slack hooks, or dashboards. Operators typically alert off HEARTBEAT / `status` (or SNMP if they load `mod_snmp`).

| Condition | Signal | Expected action |
|-----------|--------|-----------------|
| Process down | systemd unit failed; `fs_cli` cannot connect to 8021 | Check `systemctl status freeswitch`; see [Runbook](01-runbook.md) |
| Not ready | `status` says `not ready` | Core still starting or shutting down; wait or inspect log |
| Session cap | current sessions → `max-sessions` (1000 vanilla) | New calls refused; raise cap or add capacity |
| SPS cap | last SPS → `sessions-per-second` (30 vanilla) | Bursts rejected; raise SPS or shed load |
| CPU floor | `min-idle-cpu` (off unless enabled) | Calls refused when idle CPU is too low |
| SIP profile down | `sofia status` profile not running | `sofia status profile <name>`; log + siptrace |
| Log flood / disk | `freeswitch.log` rollover; rotate count 32 | Confirm HUP rotate; free disk |
| Heartbeat silent | no `HEARTBEAT` on ESL for ≫ 20s | Event system or process stuck; capture `status` and log tail |

`[NEEDS INPUT: production paging destinations and numeric thresholds for sessions / SPS / idle CPU — not defined in this tree]`

## Diagnostic Tools

Vanilla `switch.conf.xml` CLI keybindings are the intended first panel: `help`, `status`, `show channels`, `show calls`, `sofia status`, `reloadxml`, console loglevel 0/7, `sofia status profile internal`, siptrace on/off, `version`.

| Tool | Use |
|------|-----|
| `fs_cli -x status` | Live health one-liner (same as Quick Start smoke check) |
| `fs_cli -x 'sofia status'` | SIP profiles / gateways |
| `fs_cli -x 'show channels'` / `'show channels count'` | Active sessions (core SQL `channels` table) |
| `fs_cli -x 'show calls'` | Calls (not the same as channels) |
| `fs_cli -x 'show codec'` | Loaded codec interfaces |
| `fs_cli -x version` | Binary version |
| `docker logs -f --since=10s <name>` | Prefix image stdout (`-nf`); vanilla console is `info` until raised |
| `docker exec … fs_cli` | Same APIs as host `fs_cli`; use the image prefix `bin/` |
| `sofia profile <name> siptrace on` | SIP message dump into the log (noisy; key 10/11) |
| `sofia loglevel all 9` | Sofia-SIP internals 0–9 |
| ESL `event` / `filter` / `log` | External collectors; `mod_event_socket` |
| `uuid_dump <uuid>` / `uuid_buglist` | Per-session variables (via `mod_commands`) |
| Log file + `/uuid` | Follow one call |
| `mod_cdr_csv` `Master.csv` | After-the-fact hangup cause / duration |

clang **scan-build** in `.github/workflows/` is CI static analysis, not a runtime signal (see [Build](../4.development/02-build.md)).

## Operational Checklist

- [ ] Know `log_dir`: prefix `log/freeswitch.log` vs Debian `/var/log/freeswitch`
- [ ] Docker: `docker logs -f` is console stdout; fall back to `tail -F $prefix/log/freeswitch.log`
- [ ] `fs_cli -x status` shows **ready**, sessions below `max-sessions`, SPS below cap
- [ ] Console is `info` (vanilla); raise to `debug` only while reproducing; `fsctl loglevel` is the global cap
- [ ] File logger still capturing debug if you need post-mortem (vanilla mappings include `debug`)
- [ ] Correlate with **channel UUID** (`Unique-ID`, log prefix, CDR `${uuid}`); bridged calls also need `bridge_uuid` / B-leg; siptrace needs `sip_call_id`
- [ ] Sofia: `sofia status` before siptrace; turn siptrace **off** after capture; restore `console loglevel notice`
- [ ] Missing `sounds/en/us/callie/ivr/*.wav` is `mod_sndfile` WARNING, not a SIP error — `make sounds-install`
- [ ] ESL password not left at `ClueCon` on a reachable 8021
- [ ] HEARTBEAT or periodic `status` is what you would page on — there is no in-tree alert backend
- [ ] HUP/reload rotates logs (`rotate-on-hup`); confirm disk after incidents
- [ ] Do not expect Prometheus/Grafana from this git tree

## Related Documentation

- [Quick Start](../0.getting-started/02-quick-start.md) — first `fs_cli -x status`; Docker `-nf` and MoH sound install
- [Architecture](../1.architecture/01-architecture.md)
- [Build](../4.development/02-build.md) — CI including scan-build
- [Testing](../4.development/03-testing.md)
- [Runbook](01-runbook.md) — start/stop, ESL ACL, [follow one call by channel UUID](01-runbook.md#follow-one-call-by-channel-uuid)

---
<!-- PKB-metadata
last_updated: 2026-09-15
commit: e6d261c069
updated_by: human+ai
review_status: pending
review_score: 0
reviewed_by:
confidentiality: L1
-->

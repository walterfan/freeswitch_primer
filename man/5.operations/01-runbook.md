# 10. Runbook

<!-- maintained-by: human+ai -->

Ops after a working install. First-time bootstrap, Autotools, and package install live in [Quick Start](../0.getting-started/02-quick-start.md). Pipelines and prefix layout live in [Build](../4.development/02-build.md).

Vanilla XML is a **demo PBX**, not a production baseline (`conf/vanilla/README_IMPORTANT.txt`). Change SIP/VM passwords (or run `scripts/perl/randomize-passwords.pl`) and the ESL password before any public IP.

`$PREFIX` below is the configure prefix. The developer path in this PKB is
`"$HOME/fs"` configured with `--disable-fhs`, so its `bin/`, `conf/`, `mod/`,
`db/`, `log/`, and `run/` directories are below the prefix. A custom prefix
without that flag uses FHS paths; Debian packages use distro FHS paths (see
[Inspect Local State](#inspect-local-state)).

## Prerequisites

| Tool | Minimum version | Check command |
|------|-----------------|---------------|
| Installed `freeswitch` | this tree: **1.11.3-dev** | `"$PREFIX/bin/freeswitch" -version` |
| `fs_cli` (ESL client) | same prefix / package | `"$PREFIX/bin/fs_cli" -help` |
| TCP 8021 on loopback | Event Socket | `nc -z 127.0.0.1 8021` once the switch is up |
| SIP/RTP ports (if you will register or place calls) | 5060/5080 UDP/TCP, TLS 5061/5081, RTP UDP 16384–32768 | `ss -ulnp` / `netstat -ulnp` |

Source-build toolchain, Sofia-SIP, and SignalWire PAT are **not** repeated here — see [Quick Start](../0.getting-started/02-quick-start.md) if `freeswitch` is not on disk yet.

## Setup

If `make install` or a Debian package already landed binaries and config, you are done. Otherwise:

```bash
# source prefix (developer)
# see 02-quick-start.md — bootstrap / configure / make / make install

# Debian packaged daemon
# see 02-quick-start.md — FSGET + apt-get install freeswitch-meta-all
```

Confirm the single-prefix developer layout exists:

```bash
ls "$PREFIX/bin/freeswitch" "$PREFIX/bin/fs_cli" "$PREFIX/conf" "$PREFIX/mod" "$PREFIX/db" "$PREFIX/log" "$PREFIX/run"
```

Debian unit (`debian/freeswitch-systemd.freeswitch.service`) expects `/usr/bin/freeswitch`, config under `/etc/freeswitch`, logs under `/var/log/freeswitch`, state under `/var/lib/freeswitch`.

## Run the Project

### Prefix install (developer)

```bash
"$PREFIX/bin/freeswitch" -ncwait -nonat
"$PREFIX/bin/fs_cli" -x status
```

- `-ncwait`: background, no console, wait until the core is ready (`src/switch.c`).
- `-nonat`: skip UPnP/NAT-PMP pinholes (`conf/vanilla/README_IMPORTANT.txt`).
- Foreground console: `"$PREFIX/bin/freeswitch" -c -nonat`.

**Stop:**

```bash
"$PREFIX/bin/freeswitch" -stop          # reads $PREFIX/run/freeswitch.pid, SIGTERM → elegant shutdown
"$PREFIX/bin/fs_cli" -x shutdown        # API shutdown (SCSC_SHUTDOWN)
# or: kill -TERM "$(cat "$PREFIX/run/freeswitch.pid")"
```

**Reset** (wipe core SQLite; stop first):

```bash
"$PREFIX/bin/freeswitch" -stop
rm -f "$PREFIX/db"/*.db
"$PREFIX/bin/freeswitch" -ncwait -nonat
```

That drops `core.db`, Sofia registration DBs (`sofia_reg_internal.db`, `sofia_reg_external.db`), and other module SQLite files under `$PREFIX/db/` (`src/switch_core_db.c` names them `$dbdir/<dsn>.db`).

### Debian systemd

```bash
sudo systemctl start freeswitch
sudo systemctl status freeswitch
sudo systemctl stop freeswitch
sudo systemctl restart freeswitch
```

Unit start line: `/usr/bin/freeswitch -u freeswitch -g freeswitch -ncwait -nonat`. `PIDFile=/run/freeswitch/freeswitch.pid`. `DAEMON_OPTS=-nonat` unless `/etc/default/freeswitch` overrides it.

### Docker

Containers expect **host networking** (`docker/README.md`):

```bash
docker run --network host …
```

Healthcheck is `fs_cli -x status | grep -q ^UP` (`docker/base_image/healthcheck.sh`).

### Event Socket

`mod_event_socket` must be loaded (`conf/vanilla/autoload_configs/modules.conf.xml`; same in `conf/testing/`). `fs_cli` defaults (`libs/esl/fs_cli.c`, `libs/esl/fs_cli.conf`): **127.0.0.1:8021**, password **`ClueCon`**.

The module sample binds loopback (`src/mod/event_handlers/mod_event_socket/conf/autoload_configs/event_socket.conf.xml`). **Vanilla and testing runtime configs bind `listen-ip` `::`** (`conf/vanilla/autoload_configs/event_socket.conf.xml`, `conf/testing/autoload_configs/event_socket.conf.xml`) with `apply-inbound-acl` **commented out**.

Commented-out is **not** “no ACL”. If `config()` never sees that param, it still installs `loopback.auto`:

```c
if (!prefs.acl_count) {
	prefs.acl[prefs.acl_count++] = strdup("loopback.auto");
}
```

(`src/mod/event_handlers/mod_event_socket/mod_event_socket.c`). Core creates that list as default-deny with **`127.0.0.0/8`** and **`::1/128` only** (`src/switch_core.c`). Connecting to the host LAN address uses that source IP, not `127.0.0.1`, so Event Socket rejects it **before** `auth/request`. SIP `rtp-ip` / `sip-ip` are unrelated. `listen-ip` `::` is fine when `bindv6only=0` (IPv4 works on the same socket). Change the password and set an explicit inbound ACL before any non-loopback exposure — [Remote Event Socket ACL](#remote-event-socket-acl).

### HTTP/XML-RPC portal

`mod_xml_rpc` provides an embedded HTTP server, XML-RPC at `/RPC2`, and
HTTP wrappers for FreeSWITCH API commands. It does not provide a complete
administration UI by itself; static portal files are served from
`htdocs_dir`. The bundled portal assets are under `htdocs/portal/`, and
`/portal` redirects to `/portal/index.html`.

Build and install the module after enabling `xml_int/mod_xml_rpc` in the
build-time `modules.conf`:

```bash
make mod_xml_rpc
make mod_xml_rpc-install
```

Then enable it in the active runtime configuration:

```xml
<!-- $PREFIX/conf/autoload_configs/modules.conf.xml -->
<load module="mod_xml_rpc"/>
```

Configure the HTTP listener in the matching `xml_rpc.conf.xml`:

```xml
<configuration name="xml_rpc.conf" description="XML RPC">
  <settings>
    <param name="http-port" value="8080"/>
    <param name="auth-realm" value="portal"/>
    <param name="auth-user" value="portal-admin"/>
    <param name="auth-pass" value="replace-with-a-strong-password"/>
  </settings>
</configuration>
```

All three authentication parameters are required to protect non-`/pub`
requests with HTTP Basic Authentication. Put a page at
`<htdocs_dir>/portal/index.html`; inspect the directory with
`fs_cli -x "global_getvar htdocs_dir"` or override it at process start with
`-htdocs <directory>`. The page can call `/webapi/status`,
`/api/show?calls`, `/txtapi/<command>`, or `/xmlapi/<command>`.

After changing the runtime module configuration, restart FreeSWITCH or use
`unload mod_xml_rpc` followed by `load mod_xml_rpc`; `reloadxml` alone does
not load or reconfigure a module. Verify the module and endpoint:

```bash
fs_cli -x "module_exists mod_xml_rpc"
curl --user portal-admin:replace-with-a-strong-password \
  http://127.0.0.1:8080/portal/
```

Restrict port 8080 with a host/VPN firewall or an HTTPS reverse proxy. Do
not expose the demo credentials, an unrestricted API, or `/pub` content on a
public interface.

## Build and Package

Do not rebuild here. Use:

- [Quick Start](../0.getting-started/02-quick-start.md) — `./bootstrap.sh -j && ./configure --prefix=… && make && make install`
- [Build](../4.development/02-build.md) — per-module `make mod_sofia` / `make mod_sofia-install`, Debian FSDEB, Docker, Windows

Runtime load list is **not** `modules.conf`. That file (copied from `build/modules.conf.in` by `bootstrap.sh`) is **build-time**. Runtime load is `$PREFIX/conf/autoload_configs/modules.conf.xml`.

## Inspect Local State

| State | Prefix install (`--disable-fhs`, `$PREFIX`) | Debian / FHS | How to inspect |
|-------|----------------------------|--------------|----------------|
| PID | `$PREFIX/run/freeswitch.pid` | `/run/freeswitch/freeswitch.pid` | `cat …/freeswitch.pid`; `ps -p $(cat …)` |
| Process | `freeswitch` | `systemctl status freeswitch` | `ps aux \| grep '[f]reeswitch'` |
| Core SQL | `$PREFIX/db/core.db` | `/var/lib/freeswitch/db/core.db` | `ls -l …/db/`; schema in `src/switch_core_sqldb.c` (DSN `"core"`) |
| Sofia SQL | `$PREFIX/db/sofia_reg_<profile>.db` | same under FHS `db/` | `ls …/db/sofia_reg_*.db`; profiles `internal` / `external` |
| Logs | `$PREFIX/log/freeswitch.log` | `/var/log/freeswitch/freeswitch.log` | `tail -f …/freeswitch.log`; rotate on HUP (`conf/vanilla/autoload_configs/logfile.conf.xml`) |
| Console log | stdout if `-c`; else `mod_console` mappings | journal + logfile | `conf/vanilla/autoload_configs/console.conf.xml`; `fs_cli -x "console loglevel debug"` |
| Config | `$PREFIX/conf/` | `/etc/freeswitch/` | vanilla samples; flattened `freeswitch.xml.fsxml` — do not edit while running |
| Modules (`.so`) | `$PREFIX/mod/` | `${libdir}/freeswitch/mod` | `ls …/mod/mod_sofia*`; `fs_cli -x "module_exists mod_sofia"` |
| ESL | TCP 8021 | same | `ss -lntp \| grep 8021` |

Optional location overrides (same binary): `-conf`, `-log`, `-run`, `-db`, `-mod`, `-base` (`src/switch.c`).

## Verification Commands

```bash
fs_cli -x status                                    # working: a line starting with UP (docker/base_image/healthcheck.sh)
fs_cli -x version
fs_cli -x "module_exists mod_event_socket"          # true
fs_cli -x "module_exists mod_sofia"                 # true
fs_cli -x "sofia status"                            # vanilla internal / external profiles
fs_cli -x "sofia status profile internal"
fs_cli -x "show channels"
fs_cli -x "show calls"
fs_cli -x "show modules"
```

`show channels` prints the core SQL `channels` table as CSV (one row per leg). How to join A-leg and B-leg on `call_uuid`, and a captured `bridge` of 1002→1001 (WebRTC Contact): [Workflows — show channels during a bridge](../1.architecture/05-workflows.md#worked-example-show-channels-during-a-bridge).

After XML edits: `fs_cli -x reloadxml`. Directory and dialplan changes apply on the **next** call. `sofia profile internal rescan` applies supported profile data and reparses gateways, domains, and aliases, but does not rebind `sip-ip`, `sip-port`, TLS, `ws-binding`, or `wss-binding`; restart the profile or process for those changes. ACL lists need `reloadacl`. `reloadxml` does **not** load/unload modules — use `load` / `unload` / `reload mod_name` ([Chapter 5](https://developer.signalwire.com/freeswitch/configuration/module-loading/)).

Vanilla SIP ports (`conf/vanilla/vars.xml`): internal **5060** / TLS **5061**, external **5080** / TLS **5081**. RTP default range **16384–32768** (`src/switch_rtp.c`; commented overrides in `conf/vanilla/autoload_configs/switch.conf.xml`). Docker also documents UDP **64535–65535**.

Directory users **1000–1019** exist under `conf/vanilla/directory/` with shared password `1234` (`$${default_password}` in `vars.xml`) — do not expose 5060/8021 on a public IP. First-call numbers (`9196` echo, `4000` voicemail, …): [Quick Start](../0.getting-started/02-quick-start.md).

## Debugging Notes

- **CLI / ESL**: `fs_cli` first. If it cannot connect, the process is down, ESL is not on 8021, `mod_event_socket` did not load, **or the source IP failed the Event Socket ACL**. Default client target is loopback (`libs/esl/fs_cli.c`). TCP that opens then closes with `text/rude-rejection` / `Access Denied, go away.` is ACL, not “port closed” — [Remote Event Socket ACL](#remote-event-socket-acl).
- **Runtime / SIP**: `$PREFIX/log/freeswitch.log` and console mappings. Follow **one leg** by channel UUID ([Follow one call by channel UUID](#follow-one-call-by-channel-uuid)). SIP trace: `fs_cli -x "sofia profile internal siptrace on"` (keybinding 10 in `conf/vanilla/autoload_configs/switch.conf.xml`). NAT surprises: you started without `-nonat`.
- **Data / persistence**: core scoreboard is SQLite under `$PREFIX/db/` (`src/switch_core_sqldb.c`). `show channels` / `show calls` read that projection; the session thread is source of truth ([Data and API](../1.architecture/04-data-and-api.md)). Wipe `*.db` only while stopped.

Log verbosity: `fs_cli -x "console loglevel debug"`; file logger maps `all` to `console,debug,info,notice,warning,err,crit,alert` in vanilla `logfile.conf.xml`. Rotate: `fs_cli -x "fsctl send_sighup"` (`rotate-on-hup` is `true`).

Deeper log/metrics layout: [Observability](02-observability.md).

(follow-one-call-by-channel-uuid)=
## Follow one call by channel UUID

The primary key for **one FreeSWITCH leg** is the **channel UUID** (`session->uuid_str`, channel var `uuid`, event header `Unique-ID`). It is **not** the SIP `Call-ID`. Vanilla `logfile.conf.xml` sets `<param name="uuid" value="true"/>`, so `mod_logfile` prefixes file-log lines with that UUID. Grep that string to pull the leg’s application log (dialplan match, `EXECUTE`, playback, hangup).

`$log_dir/freeswitch.log` is `$PREFIX/log/freeswitch.log` on a `--disable-fhs` prefix install (Autotools default prefix `/usr/local/freeswitch`). Debian/FHS: `/var/log/freeswitch/freeswitch.log`. Confirm with `fs_cli -x "global_getvar log_dir"`.

`fs_cli` below uses the client default (`127.0.0.1:8021`, password `ClueCon`). Equivalent one-shot: `fs_cli -H 127.0.0.1 -P 8021 -p ClueCon -x '…'`. Remote LAN ACL is a separate issue — [Remote Event Socket ACL](#remote-event-socket-acl).

### Get the UUID

While the call is up:

```bash
fs_cli -x 'show channels'
```

Read `uuid`, `cid_num`, `dest`, `callstate`. One row is one **leg**. Column meanings and a two-leg `bridge`: [Workflows](../1.architecture/05-workflows.md#worked-example-show-channels-during-a-bridge).

After hangup, grep a **dialplan log marker** you added (`log` / `info` app), then take the UUID from that line. Example of a custom marker (not in vanilla):

```text
===== hit test_1005 uuid=........ from=........ =====
```

```bash
grep 'hit test_1005' "$PREFIX/log/freeswitch.log"
```

### Live: this CLI, this UUID

Inside `fs_cli`:

```text
/log 7
/uuid <channel-uuid>
```

`/log 7` is ESL debug to **this** CLI (same as `/log debug`). `/uuid` filters that stream to one UUID (`libs/esl/fs_cli.c`). Clear the filter:

```text
/uuid
```

Prefix every console line with the session UUID (does **not** filter):

```text
console uuid on
```

(`mod_console` `console uuid [on|off|toggle]`.) `fs_cli -U` / `--log-uuid` does the same for the ESL client log stream.

### After the fact: grep the file log

```bash
UUID=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
grep "$UUID" "$PREFIX/log/freeswitch.log"
grep "$UUID" "$PREFIX/log/freeswitch.log"*
```

The second command includes rotated files (`maximum-rotate` 32 in vanilla `logfile.conf.xml`).

A **single-leg** call (playback, echo, no `bridge`) is almost entirely that one UUID in the application log.

### Bridge / transfer: two UUIDs

`bridge` (and most transfers) creates an A-leg and a B-leg with **different** channel UUIDs. Grepping only one UUID drops the other half.

From the A-leg, find the peer:

```bash
fs_cli -x "uuid_getvar $UUID bridge_uuid"
fs_cli -x "uuid_dump $UUID"
```

`uuid_dump` commonly includes:

- `uuid` / event `Unique-ID`
- `bridge_uuid` / `signal_bond` (partner channel)
- `sip_call_id` (SIP layer — **not** the FS UUID)
- originator / originatee (`Other-Type`, `Other-Leg-*`)

`show channels` also joins legs on **`call_uuid`**. `show calls` is one row per bridged pair (`b_uuid`).

Grep both legs:

```bash
grep -E "$UUID_A|$UUID_B" "$PREFIX/log/freeswitch.log"
```

### SIP signaling

Application logs correlate on UUID. Packet dumps and Sofia traces correlate on **SIP Call-ID**:

```bash
fs_cli -x "uuid_getvar $UUID sip_call_id"
fs_cli -x "sofia profile internal siptrace on"
```

Then grep the log for that Call-ID. After `siptrace on`, signaling lines land in the log but **do not always** carry the channel UUID, so search UUID **and** Call-ID. Turn siptrace **off** after one reproduction.

### Align with CDR

```bash
fs_cli -x 'show calls'
```

Vanilla `mod_cdr_csv` writes `$log_dir/cdr-csv/` (`Master.csv` plus template files). The `example` / `sql` templates include `${uuid}` and `${bleg_uuid}` (`conf/vanilla/autoload_configs/cdr_csv.conf.xml`). Use those columns to join the file log to the billable record.

**Short path:** get the UUID from `show channels` or a dialplan marker → `grep "$UUID" …/freeswitch.log` (live: `fs_cli` `/uuid <uuid>`). For a bridged call, grep `bridge_uuid` (or `call_uuid`’s other row) a second time.

(remote-event-socket-acl)=
## Remote Event Socket ACL

Port **8021** listening does **not** mean a remote `fs_cli` is allowed. Sofia `rtp-ip` / `sip-ip` do not control ESL. Edit Event Socket ACL, not the SIP profile.

### Symptom

`fs_cli` never gets `auth/request`. The TCP session opens, then Event Socket closes it:

```
Content-Type: text/rude-rejection
Access Denied, go away.
```

`fs_cli` reports that as `Error Connecting []`. The listener logs `IP <src> Rejected by acl "loopback.auto"` (`mod_event_socket.c`).

Worked example on a host whose LAN address is `10.100.212.8` (adjust to the box you are on):

| Target | Result |
|--------|--------|
| `127.0.0.1:8021` | Works (`auth/request`) |
| `::1:8021` | Works |
| `10.100.212.8:8021` | Rejected (`Access Denied, go away.`) |

The last row fails **on the FreeSWITCH host itself**: connecting to the LAN IP uses source `10.100.212.8`, which is not in `loopback.auto`. A laptop on the same LAN is denied for the same reason.

### Config

Allow loopback **and** the client network in one list. Paths below are the **installed** copies under `$PREFIX/conf/` (Autotools default prefix is `/usr/local/freeswitch`). Vanilla and testing trees ship the same commented `apply-inbound-acl` (`conf/vanilla/autoload_configs/`, `conf/testing/autoload_configs/`).

**1. `autoload_configs/acl.conf.xml`** — add a dedicated list (change the LAN CIDR to match the clients that should reach ESL):

```xml
<list name="esl_allowed" default="deny">
  <node type="allow" cidr="127.0.0.1/32"/>
  <node type="allow" cidr="::1/128"/>
  <node type="allow" cidr="10.100.0.0/16"/>
</list>
```

Do not reuse the sample `lan` list (default-allow). `localnet.auto` is a guessed host mask; prefer an explicit ESL list.

**2. `autoload_configs/event_socket.conf.xml`** — set ACL explicitly:

```xml
<param name="listen-ip" value="::"/>
<param name="listen-port" value="8021"/>
<param name="password" value="ClueCon"/>
<param name="apply-inbound-acl" value="esl_allowed"/>
```

**3. Reload** (XML on disk is not live until this runs). Use loopback so reload still works while the LAN ACL is denied:

```bash
"$PREFIX/bin/fs_cli" -H 127.0.0.1 -P 8021 -p ClueCon -x "reloadacl"
"$PREFIX/bin/fs_cli" -H 127.0.0.1 -P 8021 -p ClueCon -x "reload mod_event_socket"
```

`reloadacl` rebuilds named lists; `reload mod_event_socket` re-reads `apply-inbound-acl`. `reloadxml` does not replace either step.

**4. Verify on the FreeSWITCH box first**, then from the remote client:

```bash
"$PREFIX/bin/fs_cli" -H 10.100.212.8 -P 8021 -p ClueCon -x status
```

That command fails with the default ACL even when run locally. After the change it should print `status` (a line starting with `UP`). Then retry from the laptop (`fs_cli -H <freeswitch-lan-ip> -P 8021 -p <password> -x status`).

### Security

ESL is full control of FreeSWITCH (`api`, `originate`, `shutdown`). Do not use `0.0.0.0/0` or `::/0`, and do not leave **`ClueCon`** if 8021 is reachable beyond a trusted LAN. Bind to loopback instead when remote ESL is not required.

## Common Issues

| Symptom | Cause | Fix |
|---------|-------|-----|
| `fs_cli` cannot connect | Switch not up, ESL not on 8021, or `mod_event_socket` not loaded | `"$PREFIX/bin/freeswitch" -ncwait -nonat`; `module_exists mod_event_socket`; `ss -lntp \| grep 8021` |
| `fs_cli` `Error Connecting []` to a LAN IP; `127.0.0.1:8021` works | TCP opens then `text/rude-rejection` / `Access Denied, go away.` — default ESL ACL is `loopback.auto` (commented `apply-inbound-acl` is not “no ACL”) | Allow loopback plus the client CIDR on `apply-inbound-acl`; `reloadacl` then `reload mod_event_socket` — [Remote Event Socket ACL](#remote-event-socket-acl). Do not change Sofia `rtp-ip` / `sip-ip` for `fs_cli` |
| Module missing at runtime but you expected it | **Two different files**: `modules.conf` is **build**; `modules.conf.xml` is **runtime** | Uncomment `src/mod/…` in `modules.conf`, `make mod_<name>` + install; add `<load module="mod_<name>"/>` to `autoload_configs/modules.conf.xml`; `load mod_<name>` or restart |
| `sofia status` empty / no SIP | `mod_sofia` not built or not loaded | `ls "$PREFIX/mod/mod_sofia"*`; `module_exists mod_sofia`; load it or rebuild from [Build](../4.development/02-build.md) |
| SIP bind fails / profile DOWN | Port 5060/5080 already in use, or wrong `sip-ip` | `ss -ulnp \| grep 506`; check `$${local_ip_v4}` in `conf/vanilla/vars.xml` and `sip_profiles/internal.xml` |
| One-way / no audio | RTP UDP range not open (16384–32768, plus Docker 64535–65535) | Open the range on host/firewall; Docker: `--network host` (`docker/README.md`) |
| Unexpected inbound SIP / “pinhole” | Auto NAT (UPnP/NAT-PMP) | Start with `-nonat` |
| Compromised demo extensions or ESL | Vanilla users 1000–1019; password `ClueCon`; vanilla ESL `listen-ip` `::` | Randomize SIP/VM passwords; change ESL password; bind ESL to loopback / enable ACL |
| `FSGET` / packaged apt 401 | No SignalWire PAT | PAT from `scripts/packaging/README.md`, or build from source ([Quick Start](../0.getting-started/02-quick-start.md)) |
| Docker SIP/RTP broken with `-p` | Published port ranges vs host net | `docker run --network host` (`docker/README.md`) |
| Config edit has no effect | Edited XML not reloaded; or Sofia needs profile restart | `reloadxml`; for bind/listen changes restart the profile or the process. Do not edit `freeswitch.xml.fsxml` live |
| Log grep on SIP `Call-ID` misses dialplan/`EXECUTE` | File logger prefixes **channel UUID**, not SIP Call-ID | [Follow one call by channel UUID](#follow-one-call-by-channel-uuid); grep both UUID and `sip_call_id` when siptrace is on |
| Bridged call log looks half-empty | A-leg and B-leg have different UUIDs | `uuid_getvar $UUID bridge_uuid` or join `show channels` on `call_uuid`; grep both |
| `Cannot open pid file` on `-stop` | Process already dead, or different `-run` / prefix | Confirm `$PREFIX/run/freeswitch.pid` vs `/run/freeswitch/freeswitch.pid` |
| SQLite errors after crash | Stale `$PREFIX/db/*.db` | Stop, move/remove `*.db`, start again (see Reset) |

## Related Documentation

- [Quick Start](../0.getting-started/02-quick-start.md)
- [Build](../4.development/02-build.md)
- [Testing](../4.development/03-testing.md)
- [Observability](02-observability.md)
- [Workflows](../1.architecture/05-workflows.md#worked-example-show-channels-during-a-bridge)
- [Data and API](../1.architecture/04-data-and-api.md)
- [Architecture](../1.architecture/01-architecture.md)
- [FAQ](../7.appendix/02-faq.md#how-do-i-track-a-sip-call-from-the-log)

Operator configuration: [FreeSWITCH Users Manual](https://developer.signalwire.com/freeswitch/) ([Ch 2](https://developer.signalwire.com/freeswitch/foundations/getting-started), Part 11 Troubleshooting). Historical wiki: [FreeSWITCH Explained](https://developer.signalwire.com/freeswitch/FreeSWITCH-Explained/).

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

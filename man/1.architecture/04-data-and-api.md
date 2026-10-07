# 05. Data and API

<!-- maintained-by: human+ai -->

Contracts in this tree are **not** OpenAPI/REST. Call control is ESL + console/API strings; config is XML; signaling is SIP or Verto JSON-RPC; the core scoreboard is SQL. `scripts/extract_api_signatures.py` found no Tauri/HTTP routes (`man/_generated/04-data-and-api.signatures.generated.md`) — expected for a C softswitch.

Live command list at runtime: `fs_cli -x help` (every `SWITCH_ADD_API` registration). Dialplan apps: `show applications`. Event names: `src/include/switch_types.h`. Those names are entries on the module interface bag the core looks up after `dlopen` ([Architecture — Loadable module contract](01-architecture.md#loadable-module-contract)).

## Data Model Overview

| Entity | Purpose | Primary owner | Notes |
|--------|---------|---------------|-------|
| Channel / session | One call leg | Core (`switch_core_session_t`, `switch_channel_t`) | UUID is the stable id; state `CS_*`, callstate `CCS_*` |
| Call | Pair of legs | Core SQL `calls` | `call_uuid`, `caller_uuid`, `callee_uuid` |
| XML user | SIP/VM credentials + channel vars | Directory section | Vanilla `conf/vanilla/directory/default/1000.xml` (`id`, `password`, `vm-password`, `user_context`) |
| XML extension | Dialplan match + apps | Dialplan section | `<context>` / `<extension>` / `<condition>` / `<action application="...">` |
| SIP profile | Bind, codecs, auth, ACL | `mod_sofia` + `conf/vanilla/sip_profiles/` | Internal vs external (`auth-calls`) |
| SIP registration | REGISTER state | `mod_sofia` table `sip_registrations` | Also core `registrations` |
| Voicemail message | Stored VM | `mod_voicemail` `voicemail_msgs` / `voicemail_prefs` | |
| ACD member/agent/tier | Call center | `mod_callcenter` | |
| Limit / db / group rows | Shared counters | `mod_db` | |
| Event | In-process notification | Core event engine | Header bag; `Event-Name` plus channel vars when verbose |

Runtime XML sections (vanilla root `conf/vanilla/freeswitch.xml`):

| Section | Role |
|---------|------|
| `configuration` | `autoload_configs/*.xml` (modules, sofia, ACL, ESL, …) |
| `dialplan` | Routing |
| `directory` | Users / domains (Sofia auth) |
| `chatplan` | IM routing |
| `languages` | Phrase / say XML |

Preprocessor: `#include` / `#set` / `X-PRE-PROCESS`. **`$${name}`** expands during assembly (global, `vars.xml`); **`${name}`** expands at call time on a channel ([Chapter 3](https://developer.signalwire.com/freeswitch/configuration/xml)). Flattened file: `$log_dir/freeswitch.xml.fsxml` (typically `$prefix/log/`; comment in `conf/vanilla/freeswitch.xml`). Do not edit it while running.

Modules and other autoload files are **not** opened by disk basename. `switch_xml_open_cfg("modules.conf", …)` locates `configuration` with `name="modules.conf"` inside the `configuration` section (`src/switch_xml.c`). How `<load module="mod_sofia"/>` becomes `dlopen`: [Architecture](01-architecture.md#runtime-load-tag), [Workflows](05-workflows.md#runtime-load-tag).

Directory / dialplan / SIP-profile parameter tables: [Users Manual Ch 6](https://developer.signalwire.com/freeswitch/users-and-endpoints/user-directory), [Ch 12](https://developer.signalwire.com/freeswitch/dialplan/xml), [Ch 7](https://developer.signalwire.com/freeswitch/users-and-endpoints/sip-profiles). This page lists the **contracts as implemented in this tree**, not every XML attribute.

## Storage and Persistence Rules

- **Primary storage**: SQLite core DB under the configured `db_dir` (the
  documented `--disable-fhs` developer prefix uses `$prefix/db`; custom FHS
  prefixes use `$prefix/var/lib/freeswitch/db`). Initialized in
  `switch_core_init`. Optional ODBC / `mod_pgsql` / `mod_mariadb`. Disable
  scoreboard with `freeswitch -nosql`.
- **Schema source**: SQL strings in C, not migrations. Core: `src/switch_core_sqldb.c` (`create_channels_sql`, `create_calls_sql`, `create_interfaces_sql`, `create_tasks_sql`, `create_nat_sql`, `create_registrations_sql`, `recovery_sql`, aliases/complete). Sofia: `src/mod/endpoints/mod_sofia/sofia_glue.c` (`sip_registrations`, `sip_presence`, `sip_dialogs`, `sip_subscriptions`, `sip_authentication`, shared-appearance tables).
- **Core `channels` columns** (scoreboard): `uuid`, `direction`, `created` / `created_epoch`, `name`, `state`, `cid_name` / `cid_num`, `ip_addr`, `dest`, `application` / `application_data`, `dialplan`, `context`, codecs/rates, `secure`, `hostname`, presence, `accountcode`, `callstate`, callee fields, `call_uuid`, initial-* copies. Column-by-column reading of a live `fs_cli -x 'show channels'` dump: [Workflows — show channels during a bridge](05-workflows.md#worked-example-show-channels-during-a-bridge).
- **Consistency**: one session thread owns a channel. SQL is a **projection** for `show channels` / recovery, not the source of truth. `hostname` / `switchname` isolate rows in multi-host configs.
- **CDR**: not in core SQL. Vanilla loads `mod_cdr_csv` (Asterisk-style CSV). Optional `mod_json_cdr`, `mod_xml_cdr`, `mod_cdr_sqlite`, `mod_odbc_cdr`, `mod_cdr_pg_csv`.
- **Retention**: operator-owned log/CDR rotation. `[NEEDS INPUT: site retention policy]`.

## External Interfaces

| Interface | Path or endpoint | Input | Output | Notes |
|-----------|------------------|-------|--------|-------|
| ESL (Event Socket) | Vanilla server `::`:8021; `fs_cli` client default `127.0.0.1:8021` | Line-oriented commands after `auth` | `+OK` / `-ERR`, then events | Default password `ClueCon`. Unset `apply-inbound-acl` still applies `loopback.auto` (`mod_event_socket.c` `config()`). Change bind, ACL, and password before network exposure. `parse_command()` |
| Console / `fs_cli` | Same ESL, or local console | API string (`status`, `sofia status`, `uuid_kill …`) | Text | `fs_cli -x '<api>'`; Docker health: `status` matches `^UP` |
| SIP | UDP/TCP 5060/5080 (TLS 5061/5081) | RFC 3261 + SDP | SIP + RTP | `mod_sofia`; not re-specified here |
| Verto JSON-RPC 2.0 | WS/WSS (vanilla `8081`/`8082`) | `{"jsonrpc":"2.0","method":"…","params":{}}` | JSON-RPC result/error | Methods registered in `mod_verto.c` |
| Sofia SIP over WebSocket | WS/WSS (vanilla `5066`/`7443`) | SIP over WebSocket | SIP responses and media negotiation | `ws-binding` / `wss-binding` in Sofia profiles; distinct from Verto |
| XML-RPC / HTTP | Default port **8080** | HTTP basic auth | XML-RPC | `mod_xml_rpc` **not** in vanilla autoload. Demo auth `freeswitch` / `works` in `xml_rpc.conf.xml` |
| Dynamic XML | HTTP(S) from `mod_xml_curl` | Request params (`action`, section, …) | XML fragments | Replaces file sections when enabled |
| HTTAPI | `mod_httapi` | HTTP to app server | XML instructions | Dialplan-driven HTTP IVR |

### `mod_xml_rpc` HTTP and portal surface

`mod_xml_rpc` is an in-process HTTP server, not a complete administration UI.
It registers XML-RPC at `/RPC2`, exposes FreeSWITCH API commands through
`/api/`, `/webapi/`, `/txtapi/`, and `/xmlapi/`, and serves static files from
the core `htdocs_dir`. The module redirects `/portal` to
`/portal/index.html`, so a portal page can be installed at
`<htdocs_dir>/portal/index.html` (the bundled starter assets are under
`htdocs/portal/`). The htdocs directory can be overridden with the process
`-htdocs` option.

The module is compiled when `xml_int/mod_xml_rpc` is enabled in the
build-time `modules.conf`; runtime loading is separate:
`conf/vanilla/autoload_configs/modules.conf.xml` must contain
`<load module="mod_xml_rpc"/>`, and
`conf/vanilla/autoload_configs/xml_rpc.conf.xml` controls the HTTP port and
Basic Authentication. Supplying all three `auth-realm`, `auth-user`, and
`auth-pass` parameters protects non-`/pub` HTTP requests. `/pub` is intended
for public static content and bypasses that authentication hook.

The sample credentials (`freeswitch` / `works`) are demo values. Restrict the
listener with host/VPN firewall policy or an HTTPS reverse proxy; do not
expose an unaudited portal or unrestricted API endpoint on a public interface.

### ESL command surface

Unauthenticated: `auth <password>` or `userauth user@domain:pass` (directory `esl-password` / `esl-allowed-api` / `esl-allowed-events`). Then (`parse_command` in `mod_event_socket.c`):

| Command | Role |
|---------|------|
| `api <cmd> [<args>]` | Run a console API synchronously |
| `bgapi <cmd> [<args>]` | Same, async; `Job-UUID` + `BACKGROUND_JOB` event |
| `event <plain\|json\|xml> <types\|ALL>` | Subscribe |
| `nixevent` / `noevents` | Unsubscribe |
| `myevents` | Events for one UUID |
| `sendmsg <uuid>` + headers | Execute app / hangup on a channel (`call-command`) |
| `sendevent <EVENT>` | Inject a core event |
| `getvar` | Channel variable |
| `log` / `nolog` | Log stream |
| `linger` / `nolinger` | Keep socket after hangup |
| `exit` | Close (`+OK bye`) |

Replies start with `+OK` or `-ERR`. Events serialize via `switch_event_serialize()` (plain headers) or `switch_event_serialize_json()`.

### Verto methods

Registered in `mod_verto.c`: `login`, `echo`, `jsapi`, `fsapi`, `verto.invite`, `verto.answer`, `verto.bye`, `verto.attach`, `verto.modify`, `verto.info`, `verto.ping`, `verto.subscribe`, `verto.unsubscribe`, `verto.broadcast`. Unauthenticated sockets may only `login` until `JPFLAG_AUTHED`.

### Console API (subset)

Registered with `SWITCH_ADD_API` in `src/mod/applications/mod_commands/mod_commands.c` (not exhaustive):

| API | Syntax (from registration) | Role |
|-----|----------------------------|------|
| `status` | | Process health |
| `version` | `[short]` | Build version |
| `sofia` | (sofia module) | SIP profiles / registrations |
| `originate` | (dptools/commands) | Outbound call |
| `uuid_kill` / `uuid_bridge` / `uuid_transfer` | UUID + args | Leg control |
| `show` | `channels`, `calls`, … | SQL scoreboard |
| `reloadxml` | | Re-parse XML |
| `load` / `unload` / `reload` | module name | DSOs |
| `fsctl` | | Pause, loglevel, and other core controls (`CTL_SYNTAX`) |
| `global_getvar` / `global_setvar` | | `$$` / global vars |
| `acl` | `<ip> <list_name>` | ACL test |
| `help` | | Full API list |

### Dialplan applications (subset)

`SWITCH_ADD_APP` in `src/mod/applications/mod_dptools/mod_dptools.c`: `answer`, `pre_answer`, `bridge` (`<channel_url>`), `transfer`, `sleep`, `playback`/`phrase`, `hold`/`unhold`, `park`, `set`/`export`, `hangup`, `eavesdrop`, … Other modules add `conference`, `voicemail`, `fifo`, `httapi`, etc.

Vanilla default context: `conf/vanilla/dialplan/default.xml` (authenticated internal users via `user_context=default`); public context for unauthenticated/external (`conf/vanilla/dialplan/public.xml`). Demo destinations (echo `9196`, voicemail `4000`/`*98`, …) are listed in [Quick Start](../0.getting-started/02-quick-start.md).

## Internal Commands and Events

Event ids are `switch_event_types_t` in `src/include/switch_types.h`. The engine is `src/switch_event.c` (APR FIFO + dispatch thread). Bind with `switch_event_bind`. Custom subclasses use `SWITCH_EVENT_CUSTOM`.

Core always sets header `Event-Name` (`switch_event.c`). Channel events typically include `Unique-ID`, `Channel-State`, `Caller-*`, `variable_*` when verbose (`verbose_events` app).

| Name | Trigger | Consumer | Notes |
|------|---------|----------|-------|
| `SWITCH_EVENT_STARTUP` | `switch_core_init_and_modload` | ESL, scripts | System ready |
| `SWITCH_EVENT_CHANNEL_CREATE` | Session allocated | ESL, CDR prelude | New leg |
| `SWITCH_EVENT_CHANNEL_STATE` | `CS_*` change | | |
| `SWITCH_EVENT_CHANNEL_ANSWER` / `_PROGRESS` / `_PROGRESS_MEDIA` | Signaling | | |
| `SWITCH_EVENT_CHANNEL_EXECUTE` / `_COMPLETE` | Dialplan app | | `Application` header |
| `SWITCH_EVENT_CHANNEL_BRIDGE` / `_UNBRIDGE` | Bridge | | |
| `SWITCH_EVENT_CHANNEL_HANGUP` / `_HANGUP_COMPLETE` | Hangup | CDR modules | Cause on channel |
| `SWITCH_EVENT_CHANNEL_ORIGINATE` | Outbound | | |
| `SWITCH_EVENT_DTMF` | DTMF | IVR | |
| `SWITCH_EVENT_CODEC` | Codec change | | |
| `SWITCH_EVENT_BACKGROUND_JOB` | `bgapi` done | ESL | |
| `SWITCH_EVENT_HEARTBEAT` | Core timer | Monitoring | Interval `runtime.event_heartbeat_interval` (default 20s in `switch_core_init`) |
| `SWITCH_EVENT_RELOADXML` | `reloadxml` | | |
| `SWITCH_EVENT_MODULE_LOAD` / `_UNLOAD` | DSO | | |
| `SWITCH_EVENT_API` | API invocation | | |
| `SWITCH_EVENT_RECORD_START` / `_STOP` | Record | | |
| `SWITCH_EVENT_PLAYBACK_START` / `_STOP` | Playback | | |
| `SWITCH_EVENT_CUSTOM` | Modules (Verto login, conference, …) | | Subclass name required |
| `SWITCH_EVENT_SHUTDOWN` | Process exit | | |

Do not block the event thread (`switch_event.h`): long work must queue to another thread.

Session-to-session messages (not the event bus): `switch_core_session_message_types_t` (`SWITCH_MESSAGE_INDICATE_ANSWER`, `HOLD`, `BRIDGE`, …) in `switch_types.h`.

## Validation and Compatibility

- **XML**: well-formed document after preprocessor; bad root fails startup (`switch_xml_open_root`). Dialplan conditions are regex on channel fields/vars (`mod_dialplan_xml`).
- **SIP auth**: directory digest + profile `auth-calls` + ACL (`apply-inbound-acl` → `domains` list in `acl.conf.xml`).
- **ESL**: ACL is checked **before** `auth/request`. A denied source IP gets `Content-Type: text/rude-rejection` / `Access Denied, go away.` (`mod_event_socket.c`). If `apply-inbound-acl` is unset, the default list is `loopback.auto`. After ACL allow, the client must `auth`; wrong password closes the socket (`-ERR invalid`). Remote clients: [Remote Event Socket ACL](../5.operations/01-runbook.md#remote-event-socket-acl).
- **API/apps**: stringly typed; `help` / app syntax strings are the contract. Unknown API → error text, not HTTP codes.
- **Backward compatibility**: event **names** and common headers are the long-lived contract (1.x line). Adding headers is common; removing `Event-Name` / `Unique-ID` is not. XML schema is conventional, not XSD-enforced (DTD folder `dtd/` exists for some docs). Version is `1.11.3-dev` in `configure.ac`.
- **Secrets in contracts**: vanilla directory passwords and ESL `ClueCon` are demo values — not a stable security model.

## Key Code References

- `src/include/switch_types.h` — `switch_event_types_t`, `switch_channel_state_t`
- `src/include/switch_event.h` / `src/switch_event.c` — bus, serialize, `Event-Name`
- `src/include/switch_module_interfaces.h` — `switch_endpoint_interface_t`, `switch_io_routines_t`, `switch_state_handler_table_t`, `switch_api_interface_t`, `switch_application_interface_t`
- `src/include/switch_loadable_module.h` — `switch_loadable_module_interface_t`, `SWITCH_ADD_API` / `SWITCH_ADD_APP` / `SWITCH_ADD_CHAT`
- `src/switch_core_sqldb.c` — core tables
- `src/mod/event_handlers/mod_event_socket/mod_event_socket.c` — ESL
- `src/mod/applications/mod_commands/mod_commands.c` — `SWITCH_ADD_API`
- `src/mod/applications/mod_dptools/mod_dptools.c` — `SWITCH_ADD_APP`
- `src/mod/endpoints/mod_sofia/sofia_glue.c` — SIP SQL
- `src/mod/endpoints/mod_verto/mod_verto.c` — JSON-RPC methods
- `conf/vanilla/freeswitch.xml` — XML sections
- `src/switch_xml.c` — `switch_xml_open_cfg`, `switch_xml_locate`
- `src/switch_loadable_module.c` — `<load>` walk in `switch_loadable_module_init`
- `libs/esl/` — client library (`fs_cli`)

## Related Documentation

- [Architecture](01-architecture.md)
- [Workflows](05-workflows.md)
- [Testing](../4.development/03-testing.md)
- [Users Manual Ch 46 Inbound ESL](https://developer.signalwire.com/freeswitch/programming/esl-inbound) — client bindings in `libs/esl/`

---
<!-- PKB-metadata
last_updated: 2026-09-15
commit: ca4a1b8a62
updated_by: human+ai
review_status: pending
review_score: 0
reviewed_by:
confidentiality: L1
-->

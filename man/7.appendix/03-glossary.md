# Appendix-02: Glossary

<!-- maintained-by: human+ai -->

Terms humans and AI commonly mix up in this tree. Prefer the **Notes** path over guessing.

## Terms

| Term | Meaning | Notes |
|------|---------|-------|
| `-nonat` | Start flag: skip UPnP / NAT-PMP pinholes | Required unless you *want* auto-NAT. Vanilla warning: `conf/vanilla/README_IMPORTANT.txt`. Debian unit: `debian/freeswitch-systemd.freeswitch.service`. |
| `$${name}` | Preprocessor variable, expanded while assembling XML | Defined with `#set` / `X-PRE-PROCESS cmd="set"` in `vars.xml`. Distinct from `${name}` (channel, call time). [Users Manual Ch 3](https://developer.signalwire.com/freeswitch/configuration/xml). |
| `${name}` | Channel variable, expanded at call time | Set by directory, dialplan, and apps. Catalog is Users Manual Ch 32. |
| `CS_*` | `switch_channel_state_t` values on a channel | Default inbound: `CS_NEW` → `CS_INIT` → `CS_ROUTING` → `CS_EXECUTE` → `CS_EXCHANGE_MEDIA` or hangup `CS_HANGUP` → `CS_REPORTING` → `CS_DESTROY`. Enum: `src/include/switch_types.h`. Not the same as callstate `CCS_*`. |
| ClueCon | Default ESL password | Demo value — change before public bind. Vanilla listen is `::`:8021; `fs_cli` client default is `127.0.0.1:8021`. Unset `apply-inbound-acl` still applies `loopback.auto`. `conf/vanilla/autoload_configs/event_socket.conf.xml`, `libs/esl/fs_cli.conf`. |
| bridge | Association that joins two channels so media flows between them | Dialplan app `bridge`; core `switch_ivr_bridge`. Users Manual Ch 1 term. |
| call | Pair of legs in core SQL | Table `calls`: `call_uuid`, `caller_uuid`, `callee_uuid` (`src/switch_core_sqldb.c`). Not one session. Users Manual: one or more associated channels. |
| channel | One call leg’s state, variables, hangup cause | `switch_channel_t` inside a session. Dialplan/apps key off channel state. `src/switch_channel.c`. Users Manual: one leg between FreeSWITCH and a single endpoint. |
| channel UUID | Primary key for one FreeSWITCH **leg** in logs, `uuid_*` APIs, and events | `session->uuid_str`; channel var `uuid`; event header `Unique-ID`. Vanilla `logfile.conf.xml` `uuid=true` prefixes `freeswitch.log`. **Not** SIP `Call-ID`. A `bridge` has two UUIDs (`bridge_uuid` / `signal_bond` / `call_uuid`). [Runbook](../5.operations/01-runbook.md#follow-one-call-by-channel-uuid). |
| configuration root | Directory that contains `freeswitch.xml` | Source default: `/usr/local/freeswitch/conf`; the documented `--disable-fhs` developer prefix uses `$prefix/conf`; a custom FHS prefix uses `$prefix/etc/freeswitch`. Debian: `/etc/freeswitch`. Users Manual paths are relative to this root. |
| dialplan | XML (or other) routing: match extension, run apps | Vanilla section in `conf/vanilla/freeswitch.xml`; default context `conf/vanilla/dialplan/default.xml`. Hunt in `CS_ROUTING` via `mod_dialplan_xml`. [Ch 12](https://developer.signalwire.com/freeswitch/dialplan/xml). |
| directory | XML users / domains (SIP digest, VM, channel vars) | Vanilla `conf/vanilla/directory/` (`1000.xml` …). Directory users are **not** OS users. [Ch 6](https://developer.signalwire.com/freeswitch/users-and-endpoints/user-directory). REGISTER Digest: [Workflows](../1.architecture/05-workflows.md#worked-example-register-digest-after-401). |
| Digest (SIP) | Challenge-response auth; password not sent in the clear | REGISTER: **401** `WWW-Authenticate`. INVITE (`auth-calls`): **407** `Proxy-Authenticate`. Default `algorithm=MD5`, `qop="auth"`. Code: `sofia_reg_auth_challenge` / `sofia_reg_parse_auth`. |
| endpoint | Protocol interface that originates or receives calls | A module: Sofia (`mod_sofia`) for SIP, Verto (`mod_verto`) for WebRTC. Users Manual Ch 1. |
| ESL / Event Socket | Out-of-process TCP control + event subscribe | `mod_event_socket` listens; ACL runs before `auth`; then `api` / `bgapi` / `event` / `sendmsg`. Vanilla listen `::`:8021; `fs_cli` connects to `127.0.0.1:8021`. Distinct from the in-process event bus (`src/switch_event.c`). [Ch 46](https://developer.signalwire.com/freeswitch/programming/esl-inbound). Remote ACL: [Runbook](../5.operations/01-runbook.md#remote-event-socket-acl). |
| `loopback.auto` | Built-in ACL: loopback only | Default-deny; allows `127.0.0.0/8` and `::1/128` (`src/switch_core.c`). Default ESL inbound ACL when `apply-inbound-acl` is unset. Distinct from `localnet.auto` (guessed LAN). |
| FSPR | Forked Apache Portable Runtime in `libs/apr` (`fspr_*`) | OS portability (threads, sockets, pools). Not the telephony model. [C Runtime](../1.architecture/06-c-runtime.md), [Tech Stack](../1.architecture/02-tech-stack.md). |
| FreeSWITCH | Modular C softswitch: sessions, media, dialplan, loadable DSOs | This repo’s product. Version **1.11.3-dev** (`configure.ac`). Sponsor SignalWire; hosted cloud is a *separate* product. Operator manual: [Users Manual](https://developer.signalwire.com/freeswitch/). |
| FSDEB | Build Debian packages from a **git** tree | `curl …/fsdeb \| bash -s -- -b BUILD_NUMBER -o OUT_DIR -w /path/to/freeswitch`. Tarball checkouts are not supported. `scripts/packaging/build/README.md`. |
| FSGET | Install packaged FreeSWITCH from SignalWire apt | `curl -sSL https://freeswitch.org/fsget \| bash -s <PAT> [release\|prerelease] [install]`. Needs a PAT. `scripts/packaging/README.md`, `README.md`. |
| `fs_cli` | ESL client shipped with the tree | Built from `libs/esl/fs_cli.c`. Health: `fs_cli -x status` must match `^UP`. |
| libks | KS runtime (libks / libks2), **not** in this git tree | Required if `mod_verto` or `mod_signalwire` is enabled (`configure.ac`). Often confused with bundled `libs/`. |
| `mod_sofia` | SIP endpoint module | Profiles in `conf/vanilla/sip_profiles/` (internal auth vs external `auth-calls=false`). Uses out-of-tree Sofia-SIP. `src/mod/endpoints/mod_sofia/`. Packet path: [Workflows](../1.architecture/05-workflows.md#packet-path-udptcp-to-nua). [Ch 7](https://developer.signalwire.com/freeswitch/users-and-endpoints/sip-profiles). |
| nua / nta / tport / `su_root` | Sofia-SIP UA, transaction, transport, event loop | Out-of-tree [`freeswitch/sofia-sip`](https://github.com/freeswitch/sofia-sip) `libsofia-sip-ua/`. Profile thread: `nua_create` + `su_root_step`. [Tech Stack](../1.architecture/02-tech-stack.md#sofia-sip-library). |
| `modules.conf` | **Build-time** list of `src/mod/...` trees to compile | Copied from `build/modules.conf.in` by `bootstrap.sh`. Commented lines are not built. |
| interface registry | Core hashes of named C interfaces after module load | `loadable_modules` in `src/switch_loadable_module.c`: `application_hash`, `api_hash`, `endpoint_hash`, … Lookup: `get_*_interface`. [Architecture](../1.architecture/01-architecture.md#loadable-module-contract). |
| `SWITCH_MODULE_DEFINITION` | Exported DSO table `{ SWITCH_API_VERSION, load, shutdown, runtime, flags }` | Symbol `{name}_module_interface`. After load, the module fills `switch_loadable_module_interface_t` (endpoint, API, app, …). [Architecture](../1.architecture/01-architecture.md#loadable-module-contract). |
| `modules.conf.xml` | **Runtime** autoload list of compiled modules | `conf/vanilla/autoload_configs/modules.conf.xml`. Located as `configuration name="modules.conf"` via `switch_xml_open_cfg`. Each `<load module="…"/>` is `dlopen` + `SWITCH_MODULE_DEFINITION` load. Loader: `src/switch_loadable_module.c`. Companions: `pre_load_modules.conf.xml`, `post_load_modules.conf.xml`. [Architecture](../1.architecture/01-architecture.md#runtime-load-tag). [Ch 5](https://developer.signalwire.com/freeswitch/configuration/module-loading/). |
| memory pool | APR-style arena; `switch_core_alloc` from a `switch_memory_pool_t` | Destroy the pool to free all of its allocations. Session pool is the usual call boundary (`switch_core_session_alloc`). `src/switch_core_memory.c`. [C Runtime](../1.architecture/06-c-runtime.md). |
| PKB | Project Knowledge Base (this Sphinx/MyST doc set) | Lives in **`man/`**, not `docs/man/`. Tooling: `man/pyproject.toml`. Not linked into the `freeswitch` binary. Does not replace the Users Manual. |
| prefix | Autotools install root | Default `/usr/local/freeswitch` (`AC_PREFIX_DEFAULT` in `configure.ac`). With `--disable-fhs`, layout is `bin/`, `mod/`, `conf/`, `log/`, `db/`, `scripts/`, `htdocs/`, `sounds/`; a custom prefix without that flag uses FHS `etc/`, `lib/`, `var/`, and `share/` locations. |
| RTP | Real-time media path (audio/video over UDP) | Core: `src/switch_rtp.c`, `src/switch_core_media.c`. Docker needs host networking for the UDP ranges. Media modes: [Ch 17](https://developer.signalwire.com/freeswitch/media-and-codecs/handling). |
| session | One call leg’s thread, codecs, queues, UUID | `switch_core_session_t` wraps a channel. One session thread owns the state machine. `src/switch_core_session.c`. Users Manual: runtime container for a channel and its application state. Lifetime/pool model: [C Runtime](../1.architecture/06-c-runtime.md). |
| `switch_frame_t` | One media frame after decode / before encode | `src/include/switch_frame.h`. Codecs and bridge/conference operate on frames, not SIP or raw UDP. [C Runtime](../1.architecture/06-c-runtime.md). |
| SIP Call-ID | SIP dialog identifier (`Call-ID` header) | Channel var `sip_call_id`. Use for siptrace / packet capture. Distinct from the channel UUID. |
| Sofia-SIP | External SIP stack (`sofia-sip-ua >= 1.13.18`) | **Not** vendored under `libs/`. Fork: [github.com/freeswitch/sofia-sip](https://github.com/freeswitch/sofia-sip). Used by `mod_sofia`. Map + NUA usage: [Tech Stack](../1.architecture/02-tech-stack.md#sofia-sip-library). |
| SpanDSP | External DSP / fax / codec library (`>= 3.1.1`) | **Not** in this git tree. Hard configure error if missing. `mod_spandsp`. |
| `switch_status_t` | Core C return enum (`SWITCH_STATUS_SUCCESS`, `SWITCH_STATUS_FALSE`, …) | `src/include/switch_types.h`. Endpoints may return `SWITCH_STATUS_FALSE` from a state handler to skip the core’s standard handler. Hangup *cause* is `switch_call_cause_t`, not this type. |
| three configuration domains | Directory, dialplan, and configuration — keep them distinct | Directory: who may connect. Dialplan: where a call goes and what happens. Configuration: how core and modules behave. Vanilla paths: `directory/`, `dialplan/`, `autoload_configs/` (+ `sip_profiles/`). [Overview](../0.getting-started/01-overview.md#three-configuration-domains). |
| `user_context` | Directory variable that selects the dialplan context after auth | Vanilla users 1000–1019: `default`. Overrides the SIP profile’s `context` (`public` on internal). |
| Users Manual | Operator configuration reference from SignalWire | [developer.signalwire.com/freeswitch](https://developer.signalwire.com/freeswitch/). Parts 1–8 configuration; 9 modules; 10 recipes; 11 troubleshooting; 12 ESL/scripting. Distinct from this PKB and from Explained/Confluence. |
| vanilla | Default sample XML profile (`conf/vanilla`) | Demo PBX, not a production security baseline. Other profiles: `conf/sbc`, `conf/minimal`, `conf/curl`. Installed when `$(confdir)` does not exist (`Makefile.am` `samples-conf`). |
| Verto | HTML5 / WebRTC endpoint (`mod_verto`, name `verto.rtc`) | JSON-RPC 2.0 over WS/WSS. Methods in `src/mod/endpoints/mod_verto/mod_verto.c`. Needs libks. |

## Related Documentation

- [Project Overview](../0.getting-started/01-overview.md)
- [Architecture](../1.architecture/01-architecture.md)
- [C Runtime Framework](../1.architecture/06-c-runtime.md)
- [Tech Stack](../1.architecture/02-tech-stack.md)
- [Data and API](../1.architecture/04-data-and-api.md)
- [FAQ](02-faq.md)
- [Runbook](../5.operations/01-runbook.md#follow-one-call-by-channel-uuid)
- [Users Manual](https://developer.signalwire.com/freeswitch/)

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

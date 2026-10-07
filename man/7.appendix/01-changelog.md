# Appendix-04: Documentation Changelog

<!-- maintained-by: human+ai -->

Tracks Project Knowledge Base changes (page additions, translations, tooling). Code and feature changes belong in the repository `docs/ChangeLog` and upstream release notes, not here.

## 2026-09-15

### Added

- **FS core concepts**: numbered endpoint / channel / call / bridge / session plus the three config domains (directory / dialplan / configuration), bilingual gloss (`man/0.getting-started/01-overview.md#fs-core-concepts`)
- **C Runtime Framework**: `switch_*` programming model, FSPR vs core, memory pools, session lifetime, threads/events/frames, reading order (`man/1.architecture/06-c-runtime.md`)
- Follow one call by **channel UUID** (grep `freeswitch.log`, `fs_cli` `/uuid`, A/B-leg `bridge_uuid`, SIP `Call-ID`, CDR `${uuid}` / `${bleg_uuid}`) (`man/5.operations/01-runbook.md#follow-one-call-by-channel-uuid`; Observability, FAQ, glossary)
- Worked example: `fs_cli -x 'show channels'` CSV during a two-leg `bridge` (1002 inbound → 1001 WebRTC Contact) (`man/1.architecture/05-workflows.md#worked-example-show-channels-during-a-bridge`)
- Worked example: SIP REGISTER Digest after **401** (`WWW-Authenticate`); INVITE uses **407** (`man/1.architecture/05-workflows.md#worked-example-register-digest-after-401`; FAQ, glossary)

## 2026-09-14

### Added

- Remote Event Socket ACL: commented-out `apply-inbound-acl` still defaults to `loopback.auto`; `fs_cli` `Error Connecting []` / `text/rude-rejection` (`man/5.operations/01-runbook.md#remote-event-socket-acl`, FAQ)

### Changed

- Quick Start, Data and API, Workflows, Conventions, glossary, and `index.md` now distinguish ESL listen (`::`:8021) from ESL ACL (loopback-only until `apply-inbound-acl` is set)

## 2026-09-11

### Added

- FAQ: print globals and channel vars from `fs_cli` (`global_getvar`, `uuid_dump`, `uuid_getvar`, `eval`) (`man/7.appendix/02-faq.md`)

## 2026-09-07

### Added

- Golden path **`answer`**: `SWITCH_ADD_APP` expansion, `application_hash`, hunt queue, `CS_EXECUTE` → `answer_function` (`man/1.architecture/01-architecture.md#golden-path-answer`, `man/1.architecture/05-workflows.md#worked-example-answer`)
- C-style **service registry**: modules register name→function-pointer interfaces at load (`SWITCH_ADD_*`, global hashes); no IDL (`man/1.architecture/01-architecture.md#loadable-module-contract`, `man/1.architecture/05-workflows.md#interface-registry-lookup`)
- **Loadable module contract**: in-process DSO tables (`SWITCH_MODULE_DEFINITION`, interface bag, endpoint IO/`CS_*` handlers, `SWITCH_ADD_*`) as the core extensibility mechanism (`man/1.architecture/01-architecture.md#loadable-module-contract`; Conventions, Overview, glossary)
- Runtime **`<load>` tag**: preprocessor include → `switch_xml_open_cfg("modules.conf")` → `dlopen` / `SWITCH_MODULE_DEFINITION` (`man/1.architecture/01-architecture.md#runtime-load-tag`, `man/1.architecture/05-workflows.md#runtime-load-tag`; FAQ + glossary)
- Inbound SIP **packet path**: UDP/TCP → Sofia-SIP tport/nta/nua → `sofia_event_callback` (profile thread) → session `signal_data_queue` (`man/1.architecture/05-workflows.md`, `man/1.architecture/01-architecture.md`)
- Sofia-SIP library map for out-of-tree [`freeswitch/sofia-sip`](https://github.com/freeswitch/sofia-sip) (`su`, `tport`, `nta`, `nua`, …) on [Tech Stack](../1.architecture/02-tech-stack.md)
- Sofia-SIP **NUA/NTA usage**: tag lists, minimal agent, callback/`sip_t`, stack-thread rules, mapping to `mod_sofia` ([Tech Stack](../1.architecture/02-tech-stack.md#sofia-sip-library))

### Changed

- FAQ, glossary, repo map, overview, and `index.md` PKB↔manual table now point at the Sofia-SIP section instead of only stating “not in `libs/`”

## 2026-08-17

### Changed

- Aligned PKB with the [FreeSWITCH Users Manual](https://developer.signalwire.com/freeswitch/) as the operator configuration source of truth (Explained/Confluence remain historical)
- Added PKB ↔ Users Manual page map on `index.md`; three configuration domains (directory / dialplan / configuration) on overview and architecture
- Extended Quick Start with configuration-root table, vanilla first-call numbers (`9196` echo, `1000`–`1019`), and `default_password=1234` (verified in `vars.xml`)
- Documented `$${var}` vs `${var}`, `user_context` vs SIP profile `context`, `pre_load_modules.conf.xml`, compiled XML in `log_dir`, and media late-negotiation / bypass / proxy flags
- Glossary, FAQ, runbook, AI guide, and documentation-process pages now point at Users Manual chapters instead of treating Explained as primary
- Refreshed `locale/zh_CN/LC_MESSAGES/` for the Users Manual alignment (new strings + unfuzzy of updated paragraphs)

### Added

- Added zh_CN gettext catalogs under `locale/zh_CN/LC_MESSAGES/` (all numbered pages, appendices, ADR/change templates)
- Filled `02-faq.md` and `03-glossary.md` (install, two `modules.conf`, ESL bind vs `fs_cli`, 25 terms)
- Filled `03-document.md` (PKB `man/` source of truth, Level 1–3, zh_CN catalogs)
- Filled `01-conventions.md` (`.clang-format`, SubmittingPatches, two `modules.conf`, secrets)
- Filled `02-observability.md` (`switch_log` levels, `freeswitch.log`, HEARTBEAT/`status`; no Prometheus)
- Filled `05-workflows.md` (inbound SIP, REGISTER, ESL originate, `reloadxml`)
- Filled `01-runbook.md` (start/stop/`fs_cli`, inspect `$PREFIX`, 12 common issues)
- Filled `03-testing.md` (`tests/unit/run-tests.sh`, `ci.sh`, ESL `make check`, PKB `make html-en`)
- Filled `02-build.md` (Autotools, GHA matrix, FSDEB/tarball, PKB Sphinx publish)
- Filled `04-data-and-api.md` (XML/SQL model, ESL, Verto JSON-RPC, core events; extractor found no REST)
- Filled `02-tech-stack.md` from `configure.ac` floors plus bundled lib versions (`gen_tech_stack.py` only saw PKB Sphinx)
- Filled `01-architecture.md` (C4 Context→Code, SIP session flow, module vs autoload)
- Filled `02-quick-start.md` from bootstrap/CI/Docker/packaging commands
- Filled `03-repo-map.md` from `gen_repo_map.sh` plus verified `src/mod/` category map
- Filled `01-overview.md` from repo facts (purpose, users, C4 context, scope)
- Initial bilingual Sphinx PKB skeleton under `man/` (`/PKB-init --sphinx --bilingual=zh_CN`)
- Standard numbered pages `00`–`12`, appendices, ADR/change templates, and helper scripts
- Language switcher scaffold for English and `zh_CN`

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

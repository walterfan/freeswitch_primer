# FreeSWITCH Knowledge Base

<!-- maintained-by: human+ai -->

This documentation is the Project Knowledge Base for FreeSWITCH. It is designed to help both humans and AI locate code, understand architecture, execute workflows, and verify changes.

## Overview

```{admonition} Purpose
:class: tip

This PKB should help readers:
- **Locate** code, configs, scripts, and entry points in **this git tree**
- **Understand** architecture, stack choices, and workflows
- **Execute** setup, build, release, and debugging tasks
- **Verify** behavior with tests, observability, and documentation checks
```

Operator configuration (XML, SIP profiles, dialplan, directory, codecs, ESL programming) is documented in the [FreeSWITCH Users Manual](https://developer.signalwire.com/freeswitch/). This PKB does not replace that manual. It maps the same concepts onto verified paths in this repository.

| Layer | What it is | Start here |
|-------|------------|------------|
| **Users Manual** | Operator configuration reference. Parameters and defaults are verified against FreeSWITCH source and the shipped vanilla config. | [developer.signalwire.com/freeswitch](https://developer.signalwire.com/freeswitch/) |
| **This PKB** | Repo map, C4 architecture, Autotools/CI, ESL/XML contracts as implemented here, runbook for this clone | [Project Overview](0.getting-started/01-overview.md) |
| **Historical wiki** | Older tutorials and Confluence pages still linked from `README.md` | [FreeSWITCH Explained](https://developer.signalwire.com/freeswitch/FreeSWITCH-Explained/), [Confluence](https://freeswitch.org/confluence/) |

```{admonition} How to use both
:class: note

Read [Users Manual Part 1](https://developer.signalwire.com/freeswitch/foundations/introduction) and [Part 2](https://developer.signalwire.com/freeswitch/configuration/xml) for the configuration model. Use this PKB when you need the C file, Autotools target, or CI path that implements that model.
```

## Table of Contents

```{toctree}
:maxdepth: 2
:caption: Getting Started

0.getting-started/index
```

```{toctree}
:maxdepth: 2
:caption: Architecture & Internals

1.architecture/index
```

```{toctree}
:maxdepth: 2
:caption: Signaling

2.signal/index
```

```{toctree}
:maxdepth: 2
:caption: Media

3.media/index
```

```{toctree}
:maxdepth: 2
:caption: Development

4.development/index
```

```{toctree}
:maxdepth: 2
:caption: Operations

5.operations/index
```

```{toctree}
:maxdepth: 2
:caption: Appendix

7.appendix/index
```

## PKB page to Users Manual

Keep PKB numbering stable. When an operator question is about *how to configure* FreeSWITCH, follow the Users Manual chapter; when it is about *where it lives in this tree*, stay on the PKB page.

| PKB page | Users Manual | Use the PKB for |
|----------|--------------|-----------------|
| [00 Overview](0.getting-started/01-overview.md) | [Ch 1 Core Concepts](https://developer.signalwire.com/freeswitch/foundations/introduction) | Scope of this clone; **FS core concepts** (endpoint / channel / session / call / bridge; directory / dialplan / configuration) |
| [01 Quick Start](0.getting-started/02-quick-start.md) | [Ch 2 Getting Started](https://developer.signalwire.com/freeswitch/foundations/getting-started) | Source-build from this tree; Sofia-SIP / libks out of tree |
| [02 Architecture](1.architecture/01-architecture.md) | Ch 1 + [Ch 3 XML config](https://developer.signalwire.com/freeswitch/configuration/xml) + [Ch 5 Module loading](https://developer.signalwire.com/freeswitch/configuration/module-loading/) | Session state machine, loadable-module contract, `<load>` → `dlopen`, in-process DSOs |
| [15 FreeSWITCH Modules](1.architecture/08-modules.md) | Ch 5 Module loading | How to build a module; load, initialization, runtime, shutdown, unload; `mod_sofia`, `mod_xml_rpc`, and `mod_opus` |
| [13 C Runtime](1.architecture/06-c-runtime.md) | Ch 1 (switching platform, not a generic network server) | `switch_*` programming model: pools, session lifetime, FSPR vs core, frames, events |
| [03 Tech stack](1.architecture/02-tech-stack.md) | — | Configure floors; Sofia-SIP NUA/NTA usage; SpanDSP / libks out of tree |
| [04 Repo map](1.architecture/03-repo-map.md) | Config paths relative to the directory that contains `freeswitch.xml` | `src/`, `src/mod/`, `conf/vanilla/` ownership |
| [05 Data and API](1.architecture/04-data-and-api.md) | [Ch 6 Directory](https://developer.signalwire.com/freeswitch/users-and-endpoints/user-directory), [Ch 12 XML Dialplan](https://developer.signalwire.com/freeswitch/dialplan/xml), [Ch 46 Inbound ESL](https://developer.signalwire.com/freeswitch/programming/esl-inbound) | Event ids, SQL scoreboard, ESL command table in this tree |
| [06 Workflows](1.architecture/05-workflows.md) | Ch 1 anatomy of a call; [Ch 7 SIP profiles](https://developer.signalwire.com/freeswitch/users-and-endpoints/sip-profiles) | UDP/TCP → tport/nua → INVITE/REGISTER/originate; REGISTER Digest 401; `show channels` two-leg `bridge`; name → registry → function pointer |
| [10 Runbook](5.operations/01-runbook.md) | Ch 2 + Part 11 Troubleshooting | Prefix vs FHS paths, `fs_cli` against this install, remote ESL ACL (`loopback.auto` default), follow one call by channel UUID |
| [11 Observability](5.operations/02-observability.md) | Part 11 diagnostics | `switch_log` levels, HEARTBEAT; channel UUID vs SIP Call-ID; no Prometheus in-tree |

Parts 6–10 of the Users Manual (applications, integration, module reference, recipes) have no extra PKB chapter. Link those from [Workflows](1.architecture/05-workflows.md) and [Data and API](1.architecture/04-data-and-api.md) when an operator needs parameter tables.

## Quick Start

### For New Developers

1. Read [Project Overview](0.getting-started/01-overview.md) to understand purpose, users, and scope.
2. Follow [Quick Start](0.getting-started/02-quick-start.md) to get the project running locally.
3. Study [Repository Map](1.architecture/03-repo-map.md) to locate entry points and major directories.
4. Review [Runbook](5.operations/01-runbook.md) and [Conventions](4.development/01-conventions.md) before making changes.

### For AI Assistants

Use a layered reading order:

1. **Round 1**: read `0.getting-started/01-overview`, `1.architecture/03-repo-map`, and `1.architecture/01-architecture` (plus Users Manual Ch 1 if the three configuration domains are new). For C core reading, add `1.architecture/06-c-runtime`.
2. **Round 2**: read `1.architecture/04-data-and-api` and `1.architecture/05-workflows`
3. **Round 3**: read `4.development/03-testing`, `5.operations/01-runbook`, `5.operations/02-observability`, and `5.operations/03-document`

See [How to Use This Documentation for AI](7.appendix/5.ai/01-ai-guide.md) for the detailed workflow.

## Key Concepts

:::::{grid} 2
:gutter: 3

::::{grid-item-card} Navigation
:link: 1.architecture/03-repo-map
:link-type: doc

Find the project structure, startup files, and major module boundaries.
::::

::::{grid-item-card} Architecture
:link: 1.architecture/01-architecture
:link-type: doc

Learn how the system is decomposed and how the main pieces interact.
::::

::::{grid-item-card} C Runtime
:link: 1.architecture/06-c-runtime
:link-type: doc

Read the switch_* lifetime model: pools, sessions, events, and frames.
::::

::::{grid-item-card} Workflows
:link: 1.architecture/05-workflows
:link-type: doc

Explore critical request, event, and business flows with code references.
::::

::::{grid-item-card} Testing
:link: 4.development/03-testing
:link-type: doc

Review the test layers, critical regressions, and validation commands.
::::

:::::

## Documentation Maintenance

```{admonition} Living Documentation
:class: important

Keep the PKB fresh without wasting tokens:
- run rule-based freshness checks before LLM-heavy doc work
- apply mechanical Level 1 updates first
- batch Level 2 refreshes by PR, sprint, or milestone
- keep ADRs and product rationale human-led
```

## Contributing

See [Conventions](4.development/01-conventions.md) for coding standards and [Documentation Process](5.operations/03-document.md) for PKB maintenance rules.

## Indices and tables

- {ref}`genindex`
- {ref}`search`

---

**Version**: {sub-ref}`release`
**Last Updated**: {sub-ref}`today`
<!-- PKB-metadata
last_updated: 2026-09-15
commit: e6d261c069
updated_by: human+ai
review_status: pending
review_score: 0
reviewed_by:
confidentiality: L1
-->

# 13. How to Use This Documentation for AI

<!-- maintained-by: human+ai -->

This guide explains how AI assistants should consume and use the Project Knowledge Base.

## Recommended Reading Order

1. Start with [Project Overview](../../0.getting-started/01-overview.md) for purpose, scope, and users. If the three configuration domains (directory / dialplan / configuration) are new, also read [Users Manual Chapter 1](https://developer.signalwire.com/freeswitch/foundations/introduction).
2. Read [Repository Map](../../1.architecture/03-repo-map.md) to locate entry points and important directories.
3. Read [Architecture](../../1.architecture/01-architecture.md) for cross-cutting behavior, then [C Runtime Framework](../../1.architecture/06-c-runtime.md) before tracing `switch_*` / pools / sessions, then [Tech Stack](../../1.architecture/02-tech-stack.md) for build and dependency boundaries.
4. Check [Data and API](../../1.architecture/04-data-and-api.md) and [Workflows](../../1.architecture/05-workflows.md) before changing contracts or runtime behavior.
5. Use [Conventions](../../4.development/01-conventions.md), [Build](../../4.development/02-build.md), and [Testing](../../4.development/03-testing.md) before implementing or validating changes.
6. Use [Runbook](../../5.operations/01-runbook.md) and [Observability](../../5.operations/02-observability.md) when debugging or operating the switch.
7. Use [Documentation Process](../../5.operations/03-document.md) when refreshing the PKB itself.

## The Three-Round Learning Process

### Round 1: Build the mental model

Read:

- [Project Overview](../../0.getting-started/01-overview.md)
- [Repository Map](../../1.architecture/03-repo-map.md)
- [Architecture](../../1.architecture/01-architecture.md)
- [C Runtime Framework](../../1.architecture/06-c-runtime.md) when the task is reading or changing C core / modules
- [C Runtime Framework](../../1.architecture/06-c-runtime.md)

Expected output:

1. A short system summary
2. A map of entry points and high-change modules
3. A list of missing knowledge that blocks safe implementation

### Round 2: Trace critical flows

Read:

- [Data and API](../../1.architecture/04-data-and-api.md)
- [Workflows](../../1.architecture/05-workflows.md)
- relevant code files referenced by those docs

Expected output:

1. End-to-end call chains for 2-3 critical flows
2. Boundary conditions and likely failure modes
3. A focused validation plan

### Round 3: Prepare production-safe changes

Read:

- [Documentation Process](../../5.operations/03-document.md) when the change itself updates this PKB
- [Testing](../../4.development/03-testing.md)
- [Runbook](../../5.operations/01-runbook.md)
- [Observability](../../5.operations/02-observability.md)
- [ADRs](../6.decisions/adr/index.md) when design rationale matters

Expected output:

1. A contained implementation plan
2. Validation and rollback steps
3. Any documentation that must change with the code

## When to use the Users Manual vs this PKB

| Question | Read |
|----------|------|
| What does this XML parameter mean? Default? Allowed values? | [Users Manual](https://developer.signalwire.com/freeswitch/) (Parts 1–8, Part 9 module reference) |
| Where is that implemented in **this clone**? | PKB [Repo map](../../1.architecture/03-repo-map.md), [Architecture](../../1.architecture/01-architecture.md), code references on the page |
| How do I build/test/CI this tree? | PKB [Quick Start](../../0.getting-started/02-quick-start.md), [Build](../../4.development/02-build.md), [Testing](../../4.development/03-testing.md) |
| How do I write an ESL client? | Users Manual [Ch 46 Inbound ESL](https://developer.signalwire.com/freeswitch/programming/esl-inbound) plus PKB [Data and API](../../1.architecture/04-data-and-api.md) and `libs/esl/` |
| Historical wiki / old recipes | [FreeSWITCH Explained](https://developer.signalwire.com/freeswitch/FreeSWITCH-Explained/) |

Do not paste Users Manual parameter tables into PKB pages. Link the chapter; keep PKB facts verified against this tree (file paths, C symbols, Autotools).

## Core Capabilities

### Locate

Use [Repository Map](../../1.architecture/03-repo-map.md) to find:

- startup files
- routers, handlers, and command boundaries
- scripts, tests, and release config

### Understand

Use [Architecture](../../1.architecture/01-architecture.md), [Tech Stack](../../1.architecture/02-tech-stack.md), and [Workflows](../../1.architecture/05-workflows.md) to understand:

- why the system is shaped this way
- how the main layers interact
- where important trade-offs live

### Execute

Use [Quick Start](../../0.getting-started/02-quick-start.md), [Build](../../4.development/02-build.md), and [Runbook](../../5.operations/01-runbook.md) to:

- start the project locally
- run builds and tests
- inspect state and debug failures

### Verify

Use [Testing](../../4.development/03-testing.md) and [Observability](../../5.operations/02-observability.md) to:

- choose the right validation depth
- inspect logs, metrics, traces, and alerts
- confirm whether a fix really addressed the issue

## When Updating Docs

Before refreshing PKB pages:

1. Read [Documentation Process](../../5.operations/03-document.md).
2. Prefer rule-based freshness checks first.
3. Apply zero-token Level 1 updates before asking the LLM to rewrite prose.
4. Give the LLM only the affected PKB pages, relevant diff, and source/config snippets.
5. Keep ADR rationale and product intent human-led.

## Change Checklist

Before proposing a change:

- Have you read the relevant PKB pages?
- Have you checked related [ADRs](../6.decisions/adr/index.md)?
- Does the change follow [Conventions](../../4.development/01-conventions.md)?
- Do [Testing](../../4.development/03-testing.md) and [Runbook](../../5.operations/01-runbook.md) suggest extra validation?
- Should [03-document.md](../../5.operations/03-document.md) or other PKB pages be refreshed too?

## Example Workflows

### Answering "How does authentication work?"

1. Read [Architecture](../../1.architecture/01-architecture.md).
2. Read the relevant section in [Workflows](../../1.architecture/05-workflows.md).
3. Check [Data and API](../../1.architecture/04-data-and-api.md) for contracts and models.
4. Read the code references cited by the PKB.
5. Use [Runbook](../../5.operations/01-runbook.md) and [Observability](../../5.operations/02-observability.md) for failure analysis.

### Implementing a new feature

1. Read [Project Overview](../../0.getting-started/01-overview.md) for scope.
2. Read [Architecture](../../1.architecture/01-architecture.md) and [Tech Stack](../../1.architecture/02-tech-stack.md).
3. Trace affected flows in [Workflows](../../1.architecture/05-workflows.md).
4. Plan validation using [Testing](../../4.development/03-testing.md).
5. Update relevant PKB pages after the change.

### Debugging an issue

1. Check [Runbook](../../5.operations/01-runbook.md) for common issues and inspection commands.
2. Check [Observability](../../5.operations/02-observability.md) for logs, metrics, and diagnostic signals.
3. Re-read the affected flow in [Workflows](../../1.architecture/05-workflows.md).
4. Validate the fix against [Testing](../../4.development/03-testing.md).

## Continuous Improvement

When the PKB is unclear or stale:

- report the gap explicitly
- propose the smallest doc refresh that resolves the ambiguity
- prefer targeted updates over large, generic rewrites

---

**Remember**: the goal is not to memorize everything. The goal is to know where trustworthy information lives, what still needs human input, and how to keep context small enough for accurate AI work.
<!-- PKB-metadata
last_updated: 2026-08-30
commit: d94936cc10
updated_by: human+ai
review_status: pending
review_score: 0
reviewed_by:
confidentiality: L1
-->

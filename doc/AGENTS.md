# AGENTS.md - FreeSWITCH

Modular C softswitch: sessions, media, XML dialplan, and in-process
loadable modules. This file is the operating map, not the manual.

Read this before editing. Deep architecture, workflows, and runbooks
stay in `man/` and the [Users Manual](https://developer.signalwire.com/freeswitch/).

## Context Map

- README: [`README.md`](../README.md) — product pointer and package/build links
- PKB: [`man/index.md`](../man/index.md) — start here; do not copy its content. Next: `man/0.getting-started/02-quick-start.md`, `man/1.architecture/03-repo-map.md`, `man/4.development/01-conventions.md`, `man/5.operations/01-runbook.md`. C runtime (`switch_*` / pools): `man/1.architecture/06-c-runtime.md`. SIP packet path: `man/1.architecture/05-workflows.md`; Sofia-SIP: `man/1.architecture/02-tech-stack.md`
- Patches: [`docs/SubmittingPatches`](docs/SubmittingPatches)
- Security: [`SECURITY.md`](../SECURITY.md)
- ADRs: [`man/7.appendix/6.decisions/adr/`](../man/7.appendix/6.decisions/adr/index.md)

Repo layout:

- `src/` — core (`src/switch.c` is `main`); public API `src/include/switch.h`
- `src/mod/<category>/mod_<name>/` — loadable modules
- `conf/` — XML profiles; `conf/vanilla` is a demo PBX, not production
- `libs/` — bundled APR, SRTP, ESL, VPX; Sofia-SIP, SpanDSP, and libks are **out of tree**
- `tests/unit/` — libfreeswitch unit tests
- `man/` — Sphinx PKB; `docs/man/` is Unix man pages, not the PKB
- `build/modules.conf.in` — **build-time** compile list (copied to `modules.conf`)

## Commands

Sofia-SIP, libks, SpanDSP, and signalwire-c must already be installed —
they are not in this git tree. Details: `man/0.getting-started/02-quick-start.md`.

```bash
./bootstrap.sh -j
./configure --prefix="$HOME/fs"
make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
make install                                          # samples-conf if confdir is empty
"$HOME/fs/bin/freeswitch" -ncwait -nonat
"$HOME/fs/bin/fs_cli" -x status                       # working: a line starting with UP
cd tests/unit && ./run-tests.sh                       # after configure + make + make install
make -C libs/esl check                                # ESL tests; CI group 1
cd man && poetry install && make html-en              # PKB Sphinx only, not product deps
```

There is no tree-wide lint CI gate. Match surrounding C (tabs, `.clang-format`);
do not reformat unrelated lines.

## Harness Rules

- Never fabricate paths, APIs, commands, tests, or results; inspect the
  repo or run the command first.
- Ask when ambiguity changes the output; otherwise resolve uncertainty
  by reading files and existing patterns.
- Think before coding: state assumptions, tradeoffs, and success
  criteria before non-trivial edits.
- Keep it simple: solve the requested problem without speculative
  features, one-off abstractions, or future-proofing.
- Make surgical changes: every changed line should trace to the request;
  leave unrelated code and formatting alone.
- Verify before reporting done; a plausible diff is not proof.

## Project Rules

- Do not mix `modules.conf` (build: `src/mod/...` lines) with
  `modules.conf.xml` (runtime: `<load module="mod_…"/>`) — a compiled
  module can still be unloaded.
- Do not edit `freeswitch.xml.fsxml` while the process is running — it
  is the memory-mapped compiled XML under `log_dir`.
- Do not add files to `conf/vanilla` for a new module —
  `docs/SubmittingPatches` forbids it.
- Use `$${name}` only for preprocessor vars (`vars.xml`); `${name}` is
  a call-time channel variable — mixing them is a common config bug.
- New core tests must use `src/include/test/switch_test.h`
  (`tests/unit/README`).
- Do not commit SignalWire PATs, production SIP/ESL passwords, or TLS
  keys. Vanilla `ClueCon` / `default_password=1234` are demo values.
- Report vulnerabilities to `security@signalwire.com` (`SECURITY.md`);
  do not open a public issue for an undisclosed vuln.
- Do not treat `man/pyproject.toml` as FreeSWITCH runtime dependencies.

## AI Tooling

Primary tools: Cursor and other AGENTS.md-aware clients. No
`CLAUDE.md` / `GEMINI.md` symlinks unless you ask for them.

## Keeping Current

Update this file when commands, layout, guardrails, or linked docs
move. If `man/` changes, update links here instead of copying content.
After a user correction, add or tighten one concrete rule, then prune.

<!-- last_updated: 2026-09-15 -->

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

When the user types `/graphify`, use the installed graphify skill or instructions before doing anything else.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- Dirty graphify-out/ files are expected after hooks or incremental updates; dirty graph files are not a reason to skip graphify. Only skip graphify if the task is about stale or incorrect graph output, or the user explicitly says not to use it.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).

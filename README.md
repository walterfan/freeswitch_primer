# FreeSWITCH Primer

Collect FreeSWITCH learning materials, source-reading notes, SIP/media workflows, C snippets, and a Chinese 30-day developer tutorial here.

This repository is a learning companion for FreeSWITCH. The upstream source tree and the operator configuration manual remain the authoritative references.

```bash
git clone git@github.com:walterfan/freeswitch_primer.git
cd freeswitch_primer
```

## FreeSWITCH Quick Start

1. Read the knowledge base

   * [FreeSWITCH Knowledge Base](man/index.md)
   * [Quick Start](man/0.getting-started/02-quick-start.md)
   * [Architecture](man/1.architecture/01-architecture.md)
   * [SIP and media flow](man/3.media/01-mod-sofia-media-flow.md)

2. Build the documentation and tutorial

   ```bash
   make deps
   make build-manual
   make build-tutorial
   ```

   Sphinx HTML is written to `man/_build/html/`. For the tutorial, see
   [`tutorial/README.md`](tutorial/README.md) for CMake, site, module, and
   content-validation commands.

3. Run FreeSWITCH

   This repository does not replace a FreeSWITCH installation. Clone the
   upstream source and follow the [source-build instructions](man/0.getting-started/02-quick-start.md#source-build-developer-path):

   ```bash
   git clone https://github.com/signalwire/freeswitch.git
   cd freeswitch
   ./bootstrap.sh -j
   ./configure --prefix="$HOME/fs" --disable-fhs
   make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
   make install
   "$HOME/fs/bin/freeswitch" -ncwait -nonat
   "$HOME/fs/bin/fs_cli" -x status
   ```

   Vanilla configuration is for isolated testing, not production. Change the
   demo SIP and ESL credentials, configure ACLs, and restrict SIP/RTP/ESL
   exposure before using it on an untrusted network.

## Learning Path

* **SIP and RTP** — [SIP quick reference](man/2.signal/01-sip-protocol-quick-reference.md),
  [Sofia-SIP overview](man/2.signal/02-sofia-nua-overview.md), and
  [call/media flow](man/3.media/01-mod-sofia-media-flow.md).
* **FreeSWITCH internals** — [project overview](man/0.getting-started/01-overview.md),
  [architecture](man/1.architecture/01-architecture.md), and
  [C runtime](man/1.architecture/06-c-runtime.md).
* **Operations and debugging** — [runbook](man/5.operations/01-runbook.md),
  [observability](man/5.operations/02-observability.md), and the
  [FAQ](man/7.appendix/02-faq.md).
* **Practice** — the [30-day Chinese tutorial](tutorial/content/zh-CN/syllabus.md),
  [C snippets](snippet/README.md), and focused notes under [`doc/`](doc/).

## Repository Layout

```text
man/       Sphinx Project Knowledge Base
tutorial/  Chinese 30-day tutorial, labs, module, and local teaching site
snippet/   Small C/CMake examples for FreeSWITCH runtime concepts
doc/       Focused notes and repository guidance
```

## Materials and Tools

* [FreeSWITCH source](https://github.com/signalwire/freeswitch)
* [FreeSWITCH Users Manual](https://developer.signalwire.com/freeswitch/)
* [FreeSWITCH Explained](https://developer.signalwire.com/freeswitch/FreeSWITCH-Explained/)
* [Sofia-SIP](https://github.com/freeswitch/sofia-sip)
* [Wireshark](https://www.wireshark.org/) for SIP and RTP packet analysis
* `fs_cli`, a SIP softphone, and the FreeSWITCH `vanilla` configuration for isolated experiments

## Reference

* [FreeSWITCH official site](https://freeswitch.org/)
* [FreeSWITCH GitHub repository](https://github.com/signalwire/freeswitch)
* [FreeSWITCH Users Manual](https://developer.signalwire.com/freeswitch/)
* [SIP: Session Initiation Protocol — RFC 3261](https://www.rfc-editor.org/rfc/rfc3261)
* [An Offer/Answer Model with the Session Description Protocol — RFC 3264](https://www.rfc-editor.org/rfc/rfc3264)
* [Sofia-SIP documentation](https://sofia-sip.sourceforge.net/)

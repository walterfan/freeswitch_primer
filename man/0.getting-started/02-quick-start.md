# 01. Quick Start

<!-- maintained-by: human+ai -->

Goal: get a `freeswitch` process running from **this clone** and prove it with `fs_cli -x status`. Vanilla XML is a demo PBX, not a production baseline (`conf/vanilla/README_IMPORTANT.txt`). Operator-facing first-call steps (register 1000/1001, dial `9196`) match [Users Manual Chapter 2](https://developer.signalwire.com/freeswitch/foundations/getting-started).

If you only need a packaged daemon on Debian and have a SignalWire token, skip to [Install from packages](#install-from-packages-debian). That is what `README.md` recommends for operators.

## Prerequisites

| Tool | Minimum version | Check command | Install notes |
|------|-----------------|---------------|---------------|
| Git | any recent | `git --version` | Clone this tree |
| autoconf | 2.59+ | `autoconf --version` | Enforced by `scripts/ci/build-requirements.sh` via `bootstrap.sh` |
| automake / aclocal | 1.7+ | `automake --version` | Same checker |
| libtool / libtoolize | 1.5.14+ (2.x is fine) | `libtool --version` | macOS: Homebrew `libtool` (`glibtool`) |
| C/C++ toolchain | GCC or Clang | `cc --version` | `build-essential` on Debian; Xcode CLT on macOS |
| pkg-config | any | `pkg-config --version` | Needed by `configure` |
| Sofia-SIP, libks, SpanDSP, signalwire-c | matching current FreeSWITCH | `pkg-config --exists sofia-sip-ua` | **Not fully in this repo.** CI clones [sofia-sip](https://github.com/freeswitch/sofia-sip); the Debian 11 source Dockerfile also builds libks, spandsp, and signalwire-c (`docker/examples/Debian11/Dockerfile`). |

Debian/Ubuntu source-build packages used in `docker/examples/Debian11/Dockerfile` include `build-essential`, `cmake`, `automake`, `autoconf`, `libtool-bin`, `pkg-config`, `libssl-dev`, `zlib1g-dev`, `libpcre2-dev`, `libedit-dev`, `libsqlite3-dev`, `libcurl4-openssl-dev`, `libspeexdsp-dev`, `libopus-dev`, `libsndfile1-dev`, `liblua5.2-dev`, `python3-dev`, `libpq-dev`, and related codec/AV headers.

macOS CI (`.github/workflows/macos.yml`) installs Homebrew formulas (`autoconf`, `automake`, `libtool`, `pcre2`, `sofia-sip`, `opus`, `speexdsp`, …) plus `signalwire/homebrew-signalwire/{libks2,signalwire-c2,spandsp}`.

## Clone and Install

You already have the tree if you are reading this PKB. From a fresh machine:

```bash
git clone https://github.com/signalwire/freeswitch.git
cd freeswitch
```

This workspace is version **1.11.3-dev** (`configure.ac`). Upstream default remote is `https://github.com/signalwire/freeswitch`.

### Source build (developer path)

Install Sofia-SIP (and libks / SpanDSP / signalwire-c) first, the same way CI and `docker/examples/Debian11/Dockerfile` do. Library layout and NUA usage: [Sofia-SIP](../1.architecture/02-tech-stack.md#sofia-sip-library). Then, from the repo root:

```bash
./bootstrap.sh -j
./configure --prefix="$HOME/fs" --disable-fhs
make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
make install
```

- `bootstrap.sh` copies `build/modules.conf.in` → `modules.conf` if missing, then runs Autotools. `-j` parallelizes library bootstraps.
- Default prefix without `--prefix` is `/usr/local/freeswitch` (`configure.ac`). The explicit `--disable-fhs` keeps this developer install under `$HOME/fs`; a custom prefix without that flag uses FHS paths such as `$HOME/fs/etc/freeswitch` and `$HOME/fs/var/log/freeswitch`.
- `make install` also installs vanilla sample config when `$(confdir)` does not exist (`Makefile.am`: `test -d $(DESTDIR)$(confdir) || $(MAKE) samples-conf`).
- Prompts and music-on-hold are **not** in this git tree. After `make install`, [Chapter 2](https://developer.signalwire.com/freeswitch/foundations/getting-started) also runs `make cd-sounds-install cd-moh-install` (Callie 8 kHz packages; `Makefile.am` `sounds-install`). Skip only if you do not need playback/echo tests.
- Debug symbols without changing configure flags: `./devel-bootstrap.sh` sets `CFLAGS`/`CXXFLAGS` to `-ggdb3 -O0` then bootstraps and configures.

Enable extra modules by uncommenting lines in `modules.conf` **after** bootstrap and **before** `./configure` / `make`. CI’s unit-test configure (`ci.sh`) is an example of that edit pattern.

### Install from packages (Debian)

Requires a SignalWire Personal Access Token. From `scripts/packaging/README.md`:

```bash
apt update && apt install -y curl
curl -sSL https://freeswitch.org/fsget | bash -s <PAT or API token> [release|prerelease] [install]
apt-get install -y freeswitch-meta-all
```

`release` tracks packaged releases; `prerelease` tracks what you see on `master`.

Meta packages (Users Manual Ch 2): `freeswitch-meta-all` (every module), `freeswitch-meta-vanilla` (vanilla set + typically `freeswitch-sounds-en-us-callie` / `freeswitch-sounds-music`), `freeswitch-meta-default`, `freeswitch-meta-bare`. Packaged config root is `/etc/freeswitch`; systemd: `systemctl enable --now freeswitch`.

### Docker

Packaged image (`docker/master/Dockerfile`) also needs a SignalWire `TOKEN` build-arg. The Debian 11 example is a **source image**, but its Dockerfile clones FreeSWITCH and its dependencies during the image build; it does not compile the checkout you are reading. From the repository root, build and run that example with:

```bash
./docker/examples/Debian11/freeswitch-compose.sh up
```

For a source image built from this exact checkout, use the native source-build path above or create a separate Dockerfile that copies this tree into the build context.

Runtime containers expect **host networking** (`docker/README.md`). Typical
ports: SIP 5060/5080, TLS 5061/5081, Sofia SIP WebSocket 5066/7443, Verto
WS/WSS 8081/8082, ESL **8021**, RTP UDP ranges 16384–32768 and 64535–65535.

#### Run and smoke-test `freeswitch:local` on Ubuntu

The Compose wrapper builds the Debian 11 example as `freeswitch:local` and starts one detached container:

```bash
docker run -d \
  --name freeswitch \
  --network host \
  --restart unless-stopped \
  freeswitch:local \
  /usr/local/freeswitch/bin/freeswitch -nf -nonat
```

Host networking is a Linux feature and avoids publishing the large RTP ranges individually. Do not add `-p` options when using `--network host`.

Check the container, FreeSWITCH core, and SIP profiles:

```bash
docker ps --filter name=freeswitch
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x status
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x "sofia status"
```

A healthy smoke test shows the container as `Up`, an `UP` line from `status`, and `internal` / `external` profiles from `sofia status`. If a check fails, inspect the latest logs:

```bash
docker logs --tail 100 freeswitch
```

For a call-path test, configure a SIP softphone on a machine that can reach the Ubuntu host:

```text
Username / authentication ID: 1000
Password:                     1234
Domain / server:              <Ubuntu host IP>
Port:                         5060
Transport:                    UDP
```

Confirm the registration, then dial **`9196`** and verify that speech is echoed back:

```bash
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli \
  -x "sofia status profile internal reg"
```

UDP 5060 and RTP 16384–32768 must be reachable from the softphone.

#### Install and test Music on Hold (MoH)

The source image may not include `curl`, which `build/getsounds.sh` needs to download the MoH archive. Install it in the running container, then explicitly set the download command:

```bash
docker exec -u 0 -it freeswitch bash -lc \
  'apt-get update && apt-get install -y --no-install-recommends curl'

docker exec -u 0 -it freeswitch bash -lc \
  'export DOWNLOAD_CMD="$(command -v curl) -fL -O" &&
   cd /usr/src/freeswitch &&
   make cd-moh-install'
```

Restart the existing container and call **`9664`** from the softphone. You should hear Music on Hold:

```bash
docker restart freeswitch
docker exec freeswitch /usr/local/freeswitch/bin/fs_cli -x status
```

The sound files survive `docker restart` but are lost when the container is removed and recreated. For a persistent image, add `curl` and `make cd-moh-install` to the Dockerfile build instead of installing them only in a running container.

#### Open UFW for SIP clients from any address

First check whether UFW is active. If it reports `Status: inactive`, UFW is not blocking the traffic:

```bash
sudo ufw status verbose
```

For UDP SIP and RTP from any client, add these rules:

```bash
sudo ufw allow 5060/udp comment 'FreeSWITCH SIP'
sudo ufw allow 16384:32768/udp comment 'FreeSWITCH RTP'
sudo ufw reload
sudo ufw status numbered
```

Only when clients are configured for SIP over TCP, also add:

```bash
sudo ufw allow 5060/tcp comment 'FreeSWITCH SIP TCP'
```

With `--network host`, these host firewall rules apply directly and Docker `-p` options remain unnecessary. A cloud VM also needs equivalent inbound security-group rules for UDP 5060 and UDP 16384–32768; opening UFW alone cannot bypass the provider firewall.

Opening these rules to any address exposes SIP to internet scanning and toll-fraud attempts. The vanilla users and password above are deliberately insecure demo values; replace all SIP passwords and apply authentication/rate-limiting controls first. Keep ESL port **8021** restricted to localhost or a trusted management network; Linphone does not need it. Stop and remove the test container with `docker stop freeswitch && docker rm freeswitch`.

## Run Locally

For an install under `/Users/walterfan/fs`:

```bash
/Users/walterfan/fs/bin/freeswitch -ncwait -nonat
/Users/walterfan/fs/bin/fs_cli -x status
```

- `-ncwait`: background, no console, wait until the core is ready (`src/switch.c`).
- `-nonat`: skip UPnP/NAT-PMP pinholes — required unless you *want* auto-NAT (`conf/vanilla/README_IMPORTANT.txt`).
- Foreground console instead: `freeswitch -c -nonat`.
- Graceful stop: `/Users/walterfan/fs/bin/fs_cli -x "shutdown elegant"`.
- If it will not stop, find the PID with `pgrep -f '/Users/walterfan/fs/bin/freeswitch'` and use `kill <pid>`; reserve `kill -9 <pid>` for a stuck process.

The `fs_cli` client defaults to `127.0.0.1:8021` with password `ClueCon`
(`libs/esl/fs_cli.conf`). The vanilla runtime configuration listens on
wildcard IPv6 `::`:8021 with `apply-inbound-acl` commented out
(`conf/vanilla/autoload_configs/event_socket.conf.xml`); the module sample
binds `127.0.0.1`
(`src/mod/event_handlers/mod_event_socket/conf/autoload_configs/event_socket.conf.xml`).
Commented-out is **not** “no ACL”: `mod_event_socket` still defaults to
`loopback.auto`, so `fs_cli -H <lan-ip>` is rejected until you set an explicit
list — [Remote Event Socket ACL](../5.operations/01-runbook.md#remote-event-socket-acl).
Change the password, bind, and ACL before exposing the vanilla configuration
outside a trusted management network.

The **configuration root** is the directory that contains `freeswitch.xml` ([Chapter 2](https://developer.signalwire.com/freeswitch/foundations/getting-started)):

| Install type | Typical configuration root |
|--------------|----------------------------|
| Source build (this PKB, `--prefix="$HOME/fs" --disable-fhs`) | `$HOME/fs/conf` |
| Source build (Autotools default prefix) | `/usr/local/freeswitch/conf` |
| Source build (custom prefix with default FHS) | `$prefix/etc/freeswitch` |
| Debian package | `/etc/freeswitch` |

**Working looks like:**

1. `fs_cli -x status` prints a line starting with `UP` (same check as `docker/base_image/healthcheck.sh`).
2. `fs_cli -x sofia status` shows SIP profiles (vanilla internal/external).
3. Directory users `1000`–`1019` exist in `conf/vanilla/directory/` with shared password `$${default_password}` = **`1234`** (`conf/vanilla/vars.xml`). Do not expose them on a public IP.

### First call (vanilla PBX)

Optional, after the process is `UP`. Matches Users Manual Chapter 2 against this tree’s `conf/vanilla/dialplan/default.xml`.

1. Register a SIP softphone as user `1000` (password `1234`, port **5060**, UDP or TCP) to the host’s SIP address. A second client as `1001` lets you call between extensions.
2. Confirm bindings: `fs_cli -x "show registrations"` and `fs_cli -x "sofia status profile internal reg"`.
3. From `1000`, dial **`1001`** (`Local_Extension` `^(10[01][0-9])$`) or dial **`9196`** for the echo test (no second device).

| Number | Destination in vanilla `default` context |
|--------|------------------------------------------|
| `1000`–`1019` | Registered test extensions |
| `9196` | Echo (`echo` app) |
| `9195` | Delayed echo (`delay_echo` **5000** ms in this tree — not 250 ms) |
| `9197` | Milliwatt tone (1004 Hz) |
| `9198` | Tone stream demo |
| `9664` | Music on hold |
| `5000` | IVR demo |
| `5001` | Dynamic conference |
| `3000`–`3099` | Named conferences (`30xx` narrowband) |
| `4000` or `*98` | Voicemail main (`vmain`) |
| `0` or `operator` | Operator (transfers to 1000 XML features) |

Change `default_password` in `vars.xml` before any untrusted network, then `reloadxml`. `fs_cli` number keys 1–12 map to help/status/sofia/reloadxml/siptrace (`conf/vanilla/autoload_configs/switch.conf.xml`).

## Run Tests

Unit tests are Autotools programs under `tests/unit/` linking `libfreeswitch`. CI installs first, then:

```bash
# after configure + make + make install (CI path)
cd tests/unit
./run-tests.sh                  # all tests
./run-tests.sh 2 1              # group 1 of 2, as in GitHub Actions
make -C ../../libs/esl check    # ESL tests; CI runs this on group 1
```

`./run-tests.sh` calls `make -C ../.. print_tests`, then `make -f run-tests.mk`. New tests must use `src/include/test/switch_test.h` (`tests/unit/README`).

ASAN unit-test configure (from `ci.sh -t unit-test`):

```bash
./ci.sh -t unit-test -a configure -c freeswitch
./ci.sh -t unit-test -a build -c freeswitch
./ci.sh -t unit-test -a install -c freeswitch
```

Sofia-SIP must already be built/installed for that flow (see `.github/workflows/unit-test.yml`).

## Build for Production

| Goal | Command / path |
|------|----------------|
| Unix prefix install | `./bootstrap.sh -j && ./configure --prefix=... --disable-fhs && make && make install` |
| Debian packages from a **git** tree | `scripts/packaging/build/README.md` (`FSDEB`); tarball checkouts are not supported |
| Docker (packages) | `docker/master/Dockerfile` with `TOKEN` |
| Windows | `Freeswitch.2017.sln` / `w32/` |

Install layout under the documented developer prefix (`--disable-fhs`): `bin/`, `mod/`, `conf/`, `log/`, `db/`, `scripts/`, `htdocs/`, `sounds/`. A custom prefix without `--disable-fhs` uses the FHS layout described above.

Before any public deployment: change SIP and voicemail passwords (or run `scripts/perl/randomize-passwords.pl`), change the ESL password, and start with `-nonat` unless NAT helpers are intentional.

## Common Issues

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `bootstrap.sh` exits on autoconf/automake/libtool | Tool versions below `scripts/ci/build-requirements.sh` | Install GNU autotools; on macOS use Homebrew `autoconf` `automake` `libtool` |
| `configure` / link fails on Sofia, KS, SpanDSP, or signalwire | Those libraries are out-of-tree | Build them first as in `docker/examples/Debian11/Dockerfile` or `.github/workflows/unit-test.yml` / `macos.yml` |
| `modules.conf` missing modules you expected | File is a copy of `build/modules.conf.in`; commented lines are disabled | Uncomment the `src/mod/...` path, then rebuild that module (`make mod_lua` style targets in top-level `Makefile.am`) |
| `fs_cli` cannot connect | Switch not up, or ESL not on 8021 | `freeswitch -ncwait`; confirm `mod_event_socket` loaded; `fs_cli` targets loopback but vanilla server config listens on `::` |
| `fs_cli` `Error Connecting []` to a LAN IP; `127.0.0.1` works | Default ESL ACL `loopback.auto` (commented `apply-inbound-acl` is not “no ACL”) | [Remote Event Socket ACL](../5.operations/01-runbook.md#remote-event-socket-acl) |
| Router “pinhole” / unexpected inbound SIP | Auto NAT (UPnP/NATPMP) | Start with `-nonat` |
| Compromised demo extensions | Vanilla users 1000–1019 and ESL `ClueCon` | Randomize passwords; do not publish 5060/8021 without ACLs |
| `FSGET` / `docker/master` apt repo 401 | No SignalWire PAT | Create a PAT as linked from `scripts/packaging/README.md`, or build from source |
| Docker RTP/SIP broken | Published port ranges vs host net | Use `docker run --network host` (`docker/README.md`) |
| `debian_min_build.sh` fails on modern Debian | Script pins **Jessie / FreeSWITCH 1.6** repos | Use the Debian 11 Dockerfile or current FSGET docs, not that helper |

## Next Steps

- [Overview](01-overview.md)
- [Repository Map](../1.architecture/03-repo-map.md)
- [Architecture](../1.architecture/01-architecture.md)
- [Tech Stack](../1.architecture/02-tech-stack.md)
- [Build](../4.development/02-build.md)
- [Runbook](../5.operations/01-runbook.md)

Operator configuration: [FreeSWITCH Users Manual](https://developer.signalwire.com/freeswitch/) — especially [Chapter 2 Getting Started](https://developer.signalwire.com/freeswitch/foundations/getting-started). Historical wiki: [FreeSWITCH Explained](https://developer.signalwire.com/freeswitch/FreeSWITCH-Explained/).

---
<!-- PKB-metadata
last_updated: 2026-09-14
commit: ca4a1b8a62
updated_by: human+ai
review_status: pending
review_score: 0
reviewed_by:
confidentiality: L1
-->

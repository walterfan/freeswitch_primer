# 08. Build, Release, and Publish

<!-- maintained-by: human+ai -->

Local Autotools/Windows builds, GitHub Actions, Debian/Docker packaging, version fields, and PKB HTML. First-run commands live in [Quick Start](../0.getting-started/02-quick-start.md); this page is the pipeline map. macOS Homebrew packages and flags are documented here against `.github/workflows/macos.yml`.

## Scope

- Unix source build (`bootstrap.sh` → `configure` → `make` → `make install`)
- macOS Homebrew deps (Xcode CLT + SignalWire tap) matching `macos.yml`
- Per-module rebuild, install layout, sounds
- GitHub Actions (unit tests, scan-build, Debian matrix, macOS, Windows, tarball)
- Packaged install (FSGET) and `.deb` build (FSDEB)
- Version strings and release branches
- PKB Sphinx bilingual build / AgentBox publish

Upstream operator docs: [FreeSWITCH Users Manual](https://developer.signalwire.com/freeswitch/) ([Ch 2 Getting Started](https://developer.signalwire.com/freeswitch/foundations/getting-started)). Package downloads: [files.freeswitch.org](https://files.freeswitch.org/releases/freeswitch/). Historical wiki: [FreeSWITCH Explained](https://developer.signalwire.com/freeswitch/FreeSWITCH-Explained/).

## Local Build Paths

### Development (Unix / macOS)

Prerequisites: Autotools floors in `scripts/ci/build-requirements.sh` (autoconf `>= 2.59`, automake `>= 1.7`, libtool `>= 1.5.14`) plus Sofia-SIP, SpanDSP, libks, signalwire-c as in [Tech Stack](../1.architecture/02-tech-stack.md). On macOS, install those from Homebrew first — see [macOS (Homebrew)](#macos-homebrew).

```bash
./bootstrap.sh -j          # copies build/modules.conf.in → modules.conf if missing
# edit modules.conf to enable/disable src/mod/... trees, then:
./configure --prefix="$HOME/fs" --disable-fhs
make -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)"
make install
```

Debug CFLAGS (`-ggdb3 -O0`): `./devel-bootstrap.sh` then the same `configure`/`make`.

Rebuild one module after `modules.conf` is already in place:

```bash
make mod_sofia
make mod_sofia-install
```

(Top-level `Makefile.am` dispatches `mod_*` into `src/mod`.)

ASAN unit-test configure used by CI:

```bash
./ci.sh -t unit-test -a configure -c freeswitch
./ci.sh -t unit-test -a build -c freeswitch
./ci.sh -t unit-test -a install -c freeswitch
```

(`ci.sh` also builds Sofia-SIP when `-c sofia-sip`.)

### macOS (Homebrew)

Sofia-SIP, libks, SpanDSP, and signalwire-c are **not** in this git tree. Install them from Homebrew first, then use the same Autotools path as Unix. Package list and tap names below match `.github/workflows/macos.yml`.

#### Toolchain

```bash
xcode-select --install   # if you do not already have Clang
brew --version           # Homebrew is required
```

GNU autotools floors are in `scripts/ci/build-requirements.sh` (autoconf `>= 2.59`, automake `>= 1.7`, libtool `>= 1.5.14`). On macOS use Homebrew `libtool` (`glibtool`); Apple’s stock tools are too old.

#### Homebrew packages

```bash
brew update
brew install \
  autoconf \
  automake \
  curl \
  ffmpeg@7 \
  gnu-sed \
  jpeg \
  ldns \
  libpq@18 \
  libsndfile \
  libtool \
  lua \
  opus \
  ossp-uuid \
  pcre2 \
  pkgconf \
  sofia-sip \
  speex \
  speexdsp \
  sqlite \
  yasm

brew tap signalwire/homebrew-signalwire
brew install \
  signalwire/homebrew-signalwire/libks2 \
  signalwire/homebrew-signalwire/signalwire-c2 \
  signalwire/homebrew-signalwire/spandsp

brew link --force --overwrite ffmpeg@7 libpq@18
```

CI also runs `brew install --adopt` for the SignalWire formulas (overwrite a conflicting cellar install). The runner-only `brew uninstall cmake` step is **not** needed on a developer Mac.

On Apple Silicon, Homebrew lives under `/opt/homebrew`. `ffmpeg@7` and `libpq@18` are keg-only; CI force-links them so `pkg-config` / `configure` see them. If you skip the force-link, add those prefixes to `PKG_CONFIG_PATH` (and often `PATH` / `LDFLAGS`) before `./configure`.

#### Configure, build, install

Use a home prefix so you do not need root. The command below uses
`--disable-fhs` so the installed `bin/`, `conf/`, `mod/`, `db/`, `log/`, and
`run/` directories stay below `$HOME/fs`. Default without `--prefix` is
`/usr/local/freeswitch` (`configure.ac`). macOS CI uses `--prefix=…/OUT` plus
`--enable-shared --enable-static` and therefore follows its FHS-style output
paths.

```bash
./bootstrap.sh -j
# optional: edit modules.conf here to enable/disable src/mod/... lines
./configure --prefix="$HOME/fs" --disable-fhs
make -j"$(sysctl -n hw.ncpu)"
make install
```

- `bootstrap.sh` copies `build/modules.conf.in` → `modules.conf` if missing, then runs Autotools. `-j` parallelizes bundled library bootstraps.
- Edit **`modules.conf`** (build list: `src/mod/...` lines) **after** bootstrap and **before** `./configure` / `make`. Runtime load is a separate file: `modules.conf.xml`. A compiled module can still be unloaded.
- `make install` installs vanilla sample config when `$prefix/conf` does not exist (`samples-conf`).
- Sounds / music-on-hold are **not** in git. If you need playback or the echo test: `make cd-sounds-install cd-moh-install` (see [Quick Start](../0.getting-started/02-quick-start.md)).
- Debug CFLAGS (`-ggdb3 -O0`): `./devel-bootstrap.sh` instead of `./bootstrap.sh`.

#### Run (prefix install)

```bash
"$HOME/fs/bin/freeswitch" -ncwait -nonat
"$HOME/fs/bin/fs_cli" -x status
```

A working process prints a line starting with `UP`. Stop with `"$HOME/fs/bin/freeswitch" -stop`. Vanilla is a **demo PBX**, not production: SIP users `1000`–`1019` share password `1234`; the `fs_cli` client targets `127.0.0.1:8021` by default, while vanilla ESL listens on wildcard `::`:8021 with password `ClueCon`. Start with `-nonat` unless you want UPnP NAT helpers. First-call steps: [Quick Start](../0.getting-started/02-quick-start.md).

### Production / prefix install

Same `configure && make && make install`. Default prefix is `/usr/local/freeswitch` (`AC_PREFIX_DEFAULT` in `configure.ac`). With an explicit custom prefix, `configure` enables FHS layout unless `--disable-fhs` is supplied; modules then go under `${libdir}/freeswitch/mod` and configuration under `${sysconfdir}/freeswitch`. Use [Quick Start](../0.getting-started/02-quick-start.md#source-build-developer-path) for the single-prefix developer layout.

Debian systemd unit (`debian/freeswitch-systemd.freeswitch.service`) starts:

```text
/usr/bin/freeswitch -u freeswitch -g freeswitch -ncwait -nonat
```

Sounds are **not** in this git tree. `make sounds` / `make sounds-install` downloads Callie 8 kHz packages (`Makefile.am`, default `en-us-callie-8000`). Specs at repo root: `freeswitch-sounds-*.spec`.

### Windows

```text
msbuild Freeswitch.2017.sln -t:build -verbosity:minimal -property:Configuration=Release -property:Platform=x64
```

Helper: `msbuild.cmd` (locates VS via `vswhere.exe`). Projects under `w32/`. CI uploads `x64\*.msi` on master / v1.10 / v1.11 (or PR titles containing `:upload-artifacts`).

### Expected outputs (Unix prefix)

| Path | Content |
|------|---------|
| `$prefix/bin/` | `freeswitch`, `fs_cli`, `fs_encode`, `fs_tts`, `fs_ivrd` (`bin_PROGRAMS` in `Makefile.am`) for a `--disable-fhs` install |
| `$prefix/mod/` | Loadable `.so` / `.dylib` / `.dll` for a `--disable-fhs` install |
| `$prefix/conf/` | Vanilla samples if the dir did not exist (`samples-conf`) for a `--disable-fhs` install |
| `$prefix/db/`, `log/`, `run/`, `scripts/`, `htdocs/` | Runtime dirs (`install-data-local`) for a `--disable-fhs` install; FHS installs use their configured `var/`, `share/`, `etc/`, and `lib/` locations |
| `src/include/switch_version.h` | Generated from `switch_version.h.template` |

## CI/CD Pipelines

All under `.github/workflows/`. Linux jobs use `signalwire/freeswitch-public-ci-base:bookworm-amd64` unless noted.

| Workflow | Trigger | Purpose | Key output |
|----------|---------|---------|------------|
| `ci.yml` | push `master` / `v1.10` / `v1.11`; PR open/sync; `workflow_dispatch` | Unit tests in 2 groups (`unit-test.yml` via `ci.sh`); optional DinD (`unit-test-dind.yml`); clang `scan-build.yml` | Pass/fail; scan-build reports |
| `build.yml` (“Build and Distribute”) | PR; push `master` / `v1.10` / `v1.11`; `workflow_dispatch` | Debian **bookworm** + **trixie** × amd64 / arm32v7 / arm64v8. PR excludes some ARM combos. Branch `v1.10`/`v1.11` → `release`; `master` → `unstable` | `.deb` / `.dsc` / `.changes` / tarballs; upload when push is on `signalwire/freeswitch` |
| `macos.yml` | push/PR those branches; `workflow_dispatch` | Homebrew deps + `./bootstrap.sh -j` + `./configure --prefix=OUT` + `make install` | Artifact `freeswitch-macos-build` |
| `windows.yml` | PR; push `master` / `release` | MSBuild x64 Release | MSI artifact |
| `tarball.yml` | push `v1.10` / `v1.11`; `workflow_dispatch` | `scripts/ci/src_tarball.sh` | `.tar.gz` / `.bz2` / `.xz` / `.zip` + checksums under `src_dist/`; Teleport upload on branch push |
| `unit-test.yml` | `workflow_call` from `ci.yml` | Configure/build/install Sofia-SIP + FreeSWITCH; `tests/unit/run-tests.sh`; `libs/esl` `make check` on group 1 | Logs on failure |

`build.yml` issues a temporary repo token (`repo-auth-client`) before the public Debian Docker matrix (`.github/docker/debian/<codename>/<arch>/public.<release>.Dockerfile`).

## Release Process

Version fields that **must stay together** (`configure.ac` comment):

1. `AC_INIT([freeswitch], [1.11.3-dev], …)`
2. `SWITCH_VERSION_MAJOR` / `MINOR` / `MICRO` (and optional `REVISION` / `REVISION_HUMAN` for a tagged release)
3. `build/next-release.txt` (currently `1.11.3-dev`) — `src_tarball.sh` reads this and writes `.version`

Then:

1. Land changes via GitHub PR (`docs/SubmittingPatches`, `README.md`). Topic branches; `./scripts/setup-git.sh` for author identity.
2. CI green on `ci.yml` + platform builds you care about.
3. For a packaged line: merge/tag on `v1.10` or `v1.11` so `build.yml` treats `release=release`. `master` publishes **unstable**.
4. Source archives: `tarball.yml` or `./scripts/ci/src_tarball.sh` (needs a **git** tree; FSDEB same constraint — `scripts/packaging/build/README.md`).
5. Debian packages from git: FSDEB  
   `curl -sSL https://freeswitch.org/fsdeb | bash -s -- -b BUILD_NUMBER -o OUT_DIR -w /path/to/freeswitch`
6. Operator install from SignalWire repo: FSGET (`scripts/packaging/README.md`) — requires a PAT.
7. Verify: `freeswitch -version` / `fs_cli -x version`; package lists under `freeswitch.signalwire.com` (workflow `REPO_DOMAIN`).

Human product release notes: [Confluence Release Notes](https://freeswitch.org/confluence/display/FREESWITCH/Release+Notes). Mechanical git draft: `cd man && make changelog-draft` (writes `_generated/`; slow on this history).

## Documentation Build and Publish

PKB lives in `man/` (not `docs/man` Unix pages).

```bash
cd man
poetry install
make html-en          # _build/site/en/
make gettext
make intl-update      # locale/zh_CN/LC_MESSAGES/
make html-all         # en + zh + landing; consumes tracked catalogs
make serve            # http://127.0.0.1:7008/en/ and /zh/ (needs both trees)
# make serve-watch    # English live reload only; do not click 中文 there
```

Deployment configuration is maintained outside this documentation tree. Build
the bilingual site with `make html-all`, then hand `_build/site/{en,zh}/` to
the external publishing flow. Run `make pkb-review-status-strict` separately
when a human-approval gate is required.

## Common Failures

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `bootstrap.sh` dies on autoconf/libtool | Tool older than `scripts/ci/build-requirements.sh` | Install GNU autotools; macOS Homebrew `autoconf automake libtool` (`glibtool`) |
| Link/configure missing Sofia, KS, SpanDSP, signalwire | Out-of-tree deps | Linux: `docker/examples/Debian11/Dockerfile` or `ci.sh -c sofia-sip`. macOS: Homebrew `sofia-sip` plus `signalwire/homebrew-signalwire/{libks2,signalwire-c2,spandsp}` (see [macOS (Homebrew)](#macos-homebrew)) |
| `configure` cannot find ffmpeg / libpq on macOS | Keg-only `ffmpeg@7` / `libpq@18` | `brew link --force --overwrite ffmpeg@7 libpq@18`, or set `PKG_CONFIG_PATH` |
| Module not in `mod/` after install | Commented in `modules.conf` **or** not in runtime `modules.conf.xml` | Uncomment, `make mod_*`, and load XML |
| `make dist` / FSDEB from a tarball | Scripts require git | Clone the repo (`scripts/packaging/build/README.md`) |
| FSGET / `docker/master` apt 401 | No SignalWire PAT | PAT or source build |
| Debian ffmpeg headers differ | Bookworm vs Trixie package names | `debian/control-modules` `Build-Depends-Bookworm` / `Build-Depends-Trixie` |
| Windows MSI missing in CI | Not master/v1.10/v1.11 and PR title lacks `:upload-artifacts` | See `windows.yml` `if:` |
| `scan-build-14` not found | Local machine lacks clang-14 analyzer | Use CI image or install clang tools (`ci.sh` scan-build path) |
| PKB `make html` fails | Poetry env not installed; missing `linkify-it-py` | `cd man && poetry install` |
| Changelog draft hangs | `gen_changelog.sh` walks a large git log | Run with `--range` (e.g. `HEAD~20..HEAD`) |
| Clicking 中文 404s `http://127.0.0.1:8000/zh/` | `make serve-watch` (old `make serve`) serves English `_build/html`; the switcher expects `_build/site/{en,zh}` | Stop that process; `make serve` or `make serve-all` |

## Related Documentation

- [Quick Start](../0.getting-started/02-quick-start.md)
- [Tech Stack](../1.architecture/02-tech-stack.md)
- [Testing](03-testing.md)
- [Runbook](../5.operations/01-runbook.md)
- [Documentation Process](../5.operations/03-document.md)

---
<!-- PKB-metadata
last_updated: 2026-08-25
commit: ea429c9d49
updated_by: human+ai
review_status: pending
review_score: 0
reviewed_by:
confidentiality: L1
-->

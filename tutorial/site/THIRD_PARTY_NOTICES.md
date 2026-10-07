# Third-party notices

The browser assets are served from the tutorial service itself. Runtime pages do not fetch these dependencies from a CDN.

## SIP.js 0.21.2

- Project: <https://github.com/onsip/SIP.js>
- Release: <https://github.com/onsip/SIP.js/releases/tag/0.21.2>
- License: MIT
- Local bundle: `web/vendor/sip.js/sip-0.21.2.min.js`
- License text: `web/vendor/sip.js/LICENSE.md`
- Integrity metadata: `web/vendor/sip.js/manifest.json`

The local file is the minified browser asset attached by the maintainers to the 0.21.2 GitHub release. It is not rebuilt during tutorial startup.

## markdown-it 15.0.1

- Project: <https://github.com/markdown-it/markdown-it>
- Package: <https://www.npmjs.com/package/markdown-it/v/15.0.1>
- License: MIT
- Local bundle: `web/vendor/markdown-it/markdown-it-15.0.1.esm.min.mjs`
- License text: `web/vendor/markdown-it/LICENSE.md`
- Integrity metadata: `web/vendor/markdown-it/manifest.json`

The local browser ESM bundle comes from the published npm package and renders CommonMark plus markdown-it's enabled table extension. Raw HTML remains disabled by the tutorial wrapper.

# Notes for coding agents

- Read [docs/DESIGN.md](docs/DESIGN.md) before any UI or interaction change: it holds the design rules,
  tokens, component inventory, motion and copy rules, and the verification workflow the owner expects.
- Protocol facts: [PROTOCOL.md](PROTOCOL.md). Never publish raw Bluetooth captures (addresses, link keys);
  scrub them with `tools/scrub_btsnoop.py`.
- Build and test: `./scripts/build.ps1 -Check` (native) and `node --test app/tests/model.test.js` (frontend).
- Iterate on the companion app in the browser mock (`app/index.html`); register the MSIX
  (`./scripts/package.ps1 -Register`) only when the owner asks, bumping the manifest version each time.
- Never push, open pull requests or merge without the owner's explicit permission for that action, and add
  no AI attribution to commits or pull requests.

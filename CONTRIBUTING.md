# Contributing

Build both clock configurations before submitting firmware changes. Keep SD/NVS compatibility, exact anchors, rotation mapping, Wi-Fi shutdown, and bounded allocations intact. Describe hardware checks separately from host/build checks. Use original or freely licensed minimal test content; never attach commercial books or credentials.

Open an issue describing the problem and expected behavior, or a pull request describing the change and validation. Keep changes focused. Retain third-party notices and do not commit fetched vendor files, generated binaries, device logs, or personal settings.

Documentation previews can be regenerated with Node.js and Playwright: `npm install --no-save playwright`, `npx playwright install chromium`, then `node tools/render-docs.cjs`. Preview API data is synthetic and served only on loopback.

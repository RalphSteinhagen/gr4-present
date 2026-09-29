// The viewer reaches a running state in a real browser: cross-origin isolation is in force, the WASM runtime starts,
// the launch overlay is dismissed and the canvas has been sized. Everything downstream of this assumes it.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";

const directory = process.argv[2];
if (!directory) {
    console.error("usage: qa_BrowserSmoke.mjs <viewer-web directory>");
    process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
    console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
    if (!condition) {
        failures.push(description);
    }
};

await withViewer(directory, async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html`);

    expect(await page.waitFor("window.crossOriginIsolated === true"), "the page becomes cross-origin isolated");
    expect(await page.evaluate("typeof SharedArrayBuffer !== 'undefined'"), "SharedArrayBuffer is available to the threaded runtime");
    expect(await page.waitFor("!!document.getElementById('launch')?.classList.contains('done')"), "the launch overlay is dismissed once the runtime has started");
    expect(await page.waitFor("(document.getElementById('canvas')?.width ?? 0) > 0"), "the canvas is sized");

    // only the failure prefix is asserted: the rest of this label reports the overlay's own stage and is hidden once
    // the runtime takes over, so pinning its text here would make a cosmetic change look like a regression
    const status = await page.evaluate("(document.getElementById('status')||{}).textContent || ''");
    expect(!status.startsWith("failed"), `the launch screen reports no failure, got ${JSON.stringify(status)}`);

    if (failures.length > 0) {
        console.error("console output:\n  " + page.consoleLines.join("\n  "));
    }
    await page.close();
});

process.exit(failures.length === 0 ? 0 : 1);

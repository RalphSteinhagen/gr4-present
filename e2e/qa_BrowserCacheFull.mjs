// A browser whose storage cannot take the viewer still opens the deck. Served as a plain static host serves, without
// COOP/COEP, the service worker supplies isolation and keeps a copy of each file it answers; with the site's quota
// at 1 MB (DevTools' override, which navigator.storage.estimate() does not report), the page and its glue fit and the
// WASM does not. The copy is lost, the answer is not.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserCacheFull.mjs <viewer-web directory>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const kQuota = 1_000_000;

await withViewer(
  directory,
  async ({ server, browser }) => {
    const page = await openPage(browser, "about:blank");
    await page.command("Storage.overrideQuotaForOrigin", { origin: server.origin, quotaSize: kQuota });
    await page.command("Page.navigate", { url: `${server.origin}/index.html` });
    const opened = await page.waitFor("window.crossOriginIsolated === true && (globalThis.gr4Location || '').includes('view=') ? 1 : false", 120_000).catch(() => false);
    expect(opened, "with the site's storage at 1 MB, the deck opens, isolated by the worker");
    const kept = await page.evaluate(
      "caches.keys().then(async (names) => (await Promise.all(names.map(async (name) => (await (await caches.open(name)).keys()).map((request) => new URL(request.url).pathname)))).flat().join(' '))",
    );
    expect(!kept.includes("index.wasm"), `and the WASM was too big to keep: kept ${kept || "nothing"}`);
    if (failures.length > 0) {
      console.error("console output:\n  " + page.consoleLines.slice(-30).join("\n  "));
    }
    await page.close();
  },
  { serverArguments: ["--no-isolation"] },
);

console.log(failures.length === 0 ? "qa_BrowserCacheFull: all checks passed" : `qa_BrowserCacheFull: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

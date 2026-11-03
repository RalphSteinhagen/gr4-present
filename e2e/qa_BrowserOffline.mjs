// The site works offline once visited: served without COOP/COEP, as a static host does, the service worker adds them
// and reloads once; what the page read is kept, so with the server gone the viewer and its deck still open, the
// presenter's window too, and a deck never seen shows the failure screen. The page is installable.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserOffline.mjs <viewer-web directory>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

/// navigates `page` to `url` and waits until the new document's deck is shown (or `until` holds), the old document told apart by a marker
const navigate = async (page, url, until = "(globalThis.gr4Location || '').includes('view=')", timeoutMs = 120_000) => {
  await page.evaluate("globalThis.gr4OldDocument = true");
  await page.command("Page.navigate", { url });
  return page.waitFor(`!globalThis.gr4OldDocument && (${until})`, timeoutMs).catch(() => false);
};

await withViewer(
  directory,
  async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html`);
    await page.command("Page.enable");
    expect(await page.waitFor("window.crossOriginIsolated === true", 120_000), "without headers from the host, the worker's reload makes the page cross-origin isolated");
    expect(await page.evaluate("!!navigator.serviceWorker.controller"), "and the worker controls the page");
    expect(await page.waitFor("(globalThis.gr4Location || '').includes('view=')", 120_000), "the built-in deck opens");
    const manifest = await page.command("Page.getAppManifest");
    const manifestErrors = (manifest.result?.errors ?? []).filter((error) => error.critical);
    expect(manifest.result?.url?.endsWith("manifest.webmanifest") && manifestErrors.length === 0, `the page is installable: ${JSON.stringify(manifestErrors)}`);
    const cached = await page.evaluate("caches.keys().then(async (names) => (await Promise.all(names.map(async (name) => (await (await caches.open(name)).keys()).map((request) => new URL(request.url).pathname)))).flat())");
    expect(cached.includes("/index.wasm") && cached.includes("/default/index.yml") && cached.includes("/default/talk.md"), `the viewer and the deck are kept (${cached.length} files)`);

    server.stop();
    expect(await navigate(page, `${server.origin}/index.html`), "with the server gone, the page reloads from the copy and shows the deck");
    expect(await page.evaluate("window.crossOriginIsolated === true"), "isolated as before");
    expect(await navigate(page, `${server.origin}/index.html?presenter`), "the presenter's window, a query the copy never saw, opens as well");
    const never = `${server.origin}/index.html?presentation=${encodeURIComponent("http://localhost:1/never-seen")}`;
    const logsFrom = page.consoleLines.length;
    await page.evaluate("globalThis.gr4OldDocument = true");
    await page.command("Page.navigate", { url: never });
    const reported = await page.waitFor("!globalThis.gr4OldDocument && !!document.getElementById('launch')?.classList.contains('done')", 120_000).catch(() => false);
    const deadline = Date.now() + 30_000;
    while (reported && Date.now() < deadline && !page.consoleLines.slice(logsFrom).some((line) => line.includes("no presentation here"))) {
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    expect(reported && page.consoleLines.slice(logsFrom).some((line) => line.includes("no presentation here")), "a deck never seen shows the failure screen, not the kept deck");
    expect(!(await page.evaluate("(globalThis.gr4Location || '').includes('view=')")), "and no slide of another deck");

    if (failures.length > 0) {
      console.error("console output:\n  " + page.consoleLines.slice(-40).join("\n  "));
    }
    await page.close();
  },
  { serverArguments: ["--no-isolation"] },
);

process.exit(failures.length === 0 ? 0 : 1);

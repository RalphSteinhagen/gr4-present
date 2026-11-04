// The site works offline once visited: served without COOP/COEP, as a static host does, the service worker adds them
// and reloads once; what the page read is kept, so with the server gone the viewer and its deck still open, the
// presenter's window too, and a deck never seen shows the failure screen. The page is installable.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readdirSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

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

const nextFrame = () => new Promise((resolve) => setTimeout(resolve, 100));
const click = async (page, point) => {
  await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: point.x, y: point.y });
  await nextFrame();
  for (const type of ["mousePressed", "mouseReleased"]) {
    await page.command("Input.dispatchMouseEvent", { type, x: point.x, y: point.y, button: "left", clickCount: 1 });
    await nextFrame();
  }
};
const utilityEntry = async (page, label) => {
  const probe = `(() => { const entry = (window.gr4FileEntries || []).find((e) => e[0] === "utility" && e[6].endsWith(${JSON.stringify(label)})); return entry && entry[2] >= 0 ? JSON.stringify(entry) : ""; })()`;
  for (let attempt = 0; attempt < 100; ++attempt) {
    await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 4 + (attempt % 2), y: 300 });
    const found = await page.evaluate(probe);
    if (found) {
      const entry = JSON.parse(found);
      return { x: entry[2] + entry[4] / 2, y: entry[3] + entry[5] / 2 };
    }
    await nextFrame();
  }
  throw new Error(`the side menu never showed ${label}`);
};
const untilDownload = async (folder, pattern, timeoutMs = 120_000) => {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const names = readdirSync(folder);
    const done = names.find((name) => pattern.test(name));
    if (done && !names.some((name) => name.endsWith(".crdownload"))) {
      return done;
    }
    await nextFrame();
  }
  return "";
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

    expect(cached.includes("/index.html") && cached.includes("/gr4-serviceworker.js"), "the page under its own name and the worker are kept from the start");

    server.stop();
    expect(await navigate(page, `${server.origin}/index.html`), "with the server gone, the page reloads from the copy and shows the deck");
    expect(await page.evaluate("window.crossOriginIsolated === true"), "isolated as before");
    const downloads = mkdtempSync(join(tmpdir(), "gr4-present-offline-"));
    try {
      await page.command("Browser.setDownloadBehavior", { behavior: "allow", downloadPath: downloads });
      await page.evaluate("document.getElementById('canvas').focus()");
      await click(page, await utilityEntry(page, "download viewer + deck"));
      const archive = await untilDownload(downloads, /^gr4-present.*\.tar\.gz$/);
      expect(archive !== "", `"download viewer + deck" works from the copy alone (got ${readdirSync(downloads).join(", ") || "nothing"})`);
    } finally {
      execFileSync("rm", ["-rf", downloads]);
    }
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

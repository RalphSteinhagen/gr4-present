// A deploy reaches a visitor whose browser keeps the build before. Served as GitHub Pages serves, without COOP/COEP
// and with each file kept for 600 s, the visit after the deploy still opens the kept build, while the new build's worker
// fetches the new files past the browser's cache and takes over once the page runs. No tab reloads by itself; the
// next load opens the new build, and only its files are kept.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { appendFileSync, cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserUpdate.mjs <viewer-web directory>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const kDeckShown = "(globalThis.gr4Location || '').includes('view=')";
/// navigates `page` to `url` and waits until the new document's deck is shown, the old document told apart by a marker
const navigate = async (page, url) => {
  await page.evaluate("globalThis.gr4OldDocument = true");
  await page.command("Page.navigate", { url });
  return page.waitFor(`!globalThis.gr4OldDocument && ${kDeckShown}`, 120_000).catch(() => false);
};
const kCacheNames = "caches.keys()";
const kMarker = "globalThis.gr4DeployedMarker ?? ''";

const copy = mkdtempSync(join(tmpdir(), "update-"));
try {
  cpSync(directory, copy, { recursive: true });
  await withViewer(
    copy,
    async ({ server, browser }) => {
      const page = await openPage(browser, `${server.origin}/index.html`);
      expect(await page.waitFor(`window.crossOriginIsolated === true && ${kDeckShown}`, 120_000), "the first visit opens the deck, isolated by the worker");
      const before = await page.evaluate(kCacheNames);

      // the deploy: a new worker, stamped with another build id, and new glue that marks itself
      const worker = join(copy, "gr4-serviceworker.js");
      const stamped = readFileSync(worker, "utf8");
      const restamped = stamped.replace(/const kBuild = "([^"]+)"/, 'const kBuild = "$1-next"');
      if (restamped === stamped) throw new Error("the worker's build id is not where this test expects it");
      writeFileSync(worker, restamped);
      appendFileSync(join(copy, "index.js"), '\nglobalThis.gr4DeployedMarker = "next";\n');

      expect(await navigate(page, `${server.origin}/index.html`), "the visit after the deploy opens the deck");
      expect((await page.evaluate(kMarker)) === "", "from the kept build: the new one is not used halfway through a load");
      await page.evaluate("globalThis.gr4OldDocument = true");
      const adopted = await page
        .waitFor(`${kCacheNames}.then((names) => names.length === 1 && names[0].endsWith('-next') ? names : false)`, 120_000)
        .catch(() => false);
      const workers = await page.evaluate(
        "navigator.serviceWorker.getRegistration().then((r) => JSON.stringify({ active: r?.active?.state, waiting: r?.waiting?.state, installing: r?.installing?.state, launched: document.getElementById('launch')?.classList.contains('done') }))",
      );
      expect(adopted, `once the page runs, the new build takes over and the build before is dropped: ${JSON.stringify(before)} -> ${JSON.stringify(adopted)}, workers ${workers}`);
      expect(await page.evaluate("globalThis.gr4OldDocument === true"), "and the page is not reloaded by itself");
      const keptGlue = await page.evaluate(
        "caches.keys().then(async ([name]) => (await (await caches.open(name)).match(new URL('index.js', location.href).href))?.text() ?? '')",
      );
      expect(keptGlue.includes("gr4DeployedMarker"), "the new build keeps the new glue, not the browser's cached copy of the old one");

      expect(await navigate(page, `${server.origin}/index.html`), "the next load opens the deck");
      expect((await page.evaluate(kMarker)) === "next", "with the new build");
      expect(await page.evaluate("window.crossOriginIsolated === true"), "isolated as before");

      if (failures.length > 0) {
        console.error("console output:\n  " + page.consoleLines.slice(-40).join("\n  "));
      }
      await page.close();
    },
    { serverArguments: ["--no-isolation", "--max-age", "600"] },
  );
} finally {
  rmSync(copy, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserUpdate: all checks passed" : `qa_BrowserUpdate: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

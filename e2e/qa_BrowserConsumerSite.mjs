// Another repository's site as the deck-site action makes it: served from a sub-folder of a plain static host, it opens
// its own deck, is isolated by its service worker, and no file of it names this repository's site.
// Ground truth: the deck the site was made with (test/fixtures/remote-deck opens on its view `remote`).
//
//   node e2e/qa_BrowserConsumerSite.mjs <site folder> <sub-folder>

import { launchBrowser, openPage, startServer } from "../devtools/browser_harness.mjs";
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";

const [site, folder] = process.argv.slice(2);
if (!site || !folder) {
  console.error("usage: qa_BrowserConsumerSite.mjs <site folder> <sub-folder>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const listed = (at, prefix = "") =>
  readdirSync(at, { withFileTypes: true }).flatMap((entry) => (entry.isDirectory() ? listed(join(at, entry.name), `${prefix}${entry.name}/`) : [`${prefix}${entry.name}`]));
const naming = listed(site).filter((file) => readFileSync(join(site, file)).includes("ralphsteinhagen.github.io"));
expect(naming.length === 0, `no file of the site names this repository's own site${naming.length ? `: ${naming.join(", ")}` : ""}`);

const server = await startServer(site, ["--no-isolation", "--page", `${folder}/index.html`]);
const browser = await launchBrowser();
try {
  const page = await openPage(browser, `${server.origin}/${folder}/index.html?mode=windowed`);
  expect(Boolean(await page.waitFor("(globalThis.gr4Location || '').includes('view=remote') ? 1 : false", 120_000).catch(() => false)), `the site opens its deck from /${folder}/`);
  expect((await page.evaluate("window.crossOriginIsolated === true ? 1 : 0")) === 1, "isolated by its service worker, as a plain static host sends no isolation headers");
  await page.close();
} finally {
  await browser.stop();
  server.stop();
}

console.log(failures.length === 0 ? "qa_BrowserConsumerSite: all checks passed" : `qa_BrowserConsumerSite: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

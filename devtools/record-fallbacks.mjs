// Records what each live region shows, so the viewer has something to put in its place when the region cannot run:
// a graph that fails to build, a workflow that is missing, or one that stops making progress.
//
//   node devtools/record-fallbacks.mjs <viewer-web directory> <package directory> [--write]
//
// Walks the deck in a headless browser, waits on every view with live regions until their charts have run, and crops
// each region into <package directory>/fallback/<view>-<region>.webp (ImageMagick does the crop and the encoding). With
// --write it also adds `fallback: fallback/<view>-<region>.webp` to every `:::gr4` block that has none. A recording
// goes stale when the slide changes, so it is made again before a talk rather than kept as a source of truth.

import { openPage, withViewer } from "./browser_harness.mjs";
import { execFileSync } from "node:child_process";
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

// the staged deck is what is shown; the package it was staged from is where the recordings belong
const [directory, packageDirectory] = process.argv.slice(2).filter((argument) => !argument.startsWith("--"));
const write = process.argv.includes("--write");
if (!directory || !packageDirectory) {
  console.error("usage: record-fallbacks.mjs <viewer-web directory> <package directory> [--write]");
  process.exit(2);
}
const kSettleFrames = 90; // screenshots to wait on a live view, so its charts have drawn some data
const kRight = ["ArrowRight", "ArrowRight", 39];

const scratch = mkdtempSync(join(tmpdir(), "fallbacks-"));
const recorded = []; // [view, region, file]
try {
  await withViewer(directory, async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html`);
    await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
    await page.waitFor("location.hash.includes('view=') && location.hash.includes('step=0')");
    await page.evaluate("document.getElementById('canvas').focus()");
    await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 1100, y: 600 });
    const seen = new Set();
    for (let press = 0; press < 400; ++press) {
      const hash = await page.evaluate("location.hash");
      // the address names a view by its footer number; the id it is recorded and written under is published here
      const view = (await page.evaluate("(globalThis.gr4Location || '')")).replace(/^#?view=/, "").replace(/&.*$/, "");
      const regions = JSON.parse(await page.evaluate("JSON.stringify(window.gr4Regions || {})"));
      if (Object.keys(regions).length > 0 && !seen.has(view)) {
        seen.add(view);
        let shot = "";
        for (let frame = 0; frame < kSettleFrames; ++frame) {
          // the side menu opens at the left edge and a synthesised pointer does not reliably leave it, so it is moved
          // away again and again while the charts fill, or the menu ends up in the recording
          if (frame % 10 === 0) {
            await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 1100 + (frame % 20), y: 600 });
          }
          shot = (await page.command("Page.captureScreenshot", { format: "png" })).result.data;
        }
        const whole = join(scratch, `${view}.png`);
        writeFileSync(whole, Buffer.from(shot, "base64"));
        const placed = JSON.parse(await page.evaluate("JSON.stringify(window.gr4Regions || {})"));
        mkdirSync(join(packageDirectory, "fallback"), { recursive: true });
        for (const [region, [x, y, width, height]] of Object.entries(placed)) {
          const file = `fallback/${view}-${region}.webp`;
          execFileSync("magick", [whole, "-crop", `${width}x${height}+${x}+${y}`, "+repage", "-quality", "90", join(packageDirectory, file)]);
          recorded.push([view, region, file]);
          console.log(`recorded ${file}`);
        }
      }
      await page.press(...kRight);
      if (!(await page.waitFor(`location.hash !== ${JSON.stringify(hash)} ? 1 : false`, 3000).catch(() => false))) {
        break; // the end of the deck
      }
    }
    await page.close();
  });
} finally {
  rmSync(scratch, { recursive: true, force: true });
}

if (write) {
  const talk = join(packageDirectory, "talk.md");
  let text = readFileSync(talk, "utf8");
  for (const [view, region, file] of recorded) {
    const section = text.indexOf(`{#${view}}`);
    const block = text.indexOf(`:::gr4\nid: ${region}\n`, section);
    const close = text.indexOf("\n:::", block + 1);
    if (section < 0 || block < 0 || close < 0 || text.slice(block, close).includes("\nfallback:")) {
      continue;
    }
    text = text.slice(0, close) + `\nfallback: ${file}` + text.slice(close);
  }
  writeFileSync(talk, text);
  console.log(`added fallback: lines to ${talk}`);
}

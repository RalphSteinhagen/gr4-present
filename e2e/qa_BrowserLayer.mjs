// A box with `layer=above` lies over a live chart; without it the chart, its own window, is drawn over every box.
// Ground truth: a running chart changes from frame to frame and a framed box with fixed words does not, so the box's
// area stands still exactly when the box is on top.

import { kHeadlessQuery, openPage, withViewer } from "../devtools/browser_harness.mjs";
import { cpSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserLayer.mjs <viewer-web directory>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const kWidth = 1280;
const kHeight = 720;
// the box's area, well inside it so its border and the chart's edge stay out: 0.35..0.65 of the slide
const kClip = { x: 0.4 * kWidth, y: 0.4 * kHeight, width: 0.2 * kWidth, height: 0.2 * kHeight, scale: 1 };

const deckWith = (layer) => `# Layer {#layer}

:::place
chart: 0.05 0.05 0.90 0.90
cover: 0.30 0.30 0.40 0.40
:::

:::gr4
id: charts
region: chart
workflow: workflows/controlled-sines.grc
lifecycle: run-while-visible
:::

:::cover {frame ${layer}}
A box over the chart.
:::
`;

/// how many of the screenshots over four seconds differ from the one before, in the box's area
const changesOver = async (page) => {
  let previous = "";
  let changes = 0;
  const started = Date.now();
  while (Date.now() - started < 4000) {
    const shot = (await page.command("Page.captureScreenshot", { format: "png", clip: kClip })).result.data;
    changes += previous !== "" && shot !== previous ? 1 : 0;
    previous = shot;
  }
  return changes;
};

const copy = mkdtempSync(join(tmpdir(), "layer-"));
try {
  cpSync(directory, copy, { recursive: true });
  const changes = {};
  for (const layer of ["", "layer=above"]) {
    writeFileSync(join(copy, "default", "talk.md"), deckWith(layer));
    await withViewer(copy, async ({ server, browser }) => {
      const page = await openPage(browser, `${server.origin}/index.html?${kHeadlessQuery}#view=layer&step=0`);
      await page.command("Emulation.setDeviceMetricsOverride", { width: kWidth, height: kHeight, deviceScaleFactor: 1, mobile: false });
      await page.waitFor("(globalThis.gr4Location || '').includes('view=layer') ? 1 : false");
      for (let frame = 0; frame < 90; ++frame) {
        await page.command("Page.captureScreenshot", { format: "png" }); // the graph starts and its charts fill
      }
      changes[layer || "none"] = await changesOver(page);
      await page.close();
    });
  }
  expect(changes.none >= 5, `without a layer, the chart shows through the box: ${changes.none} changes in its area over 4 s`);
  expect(changes["layer=above"] <= 1, `with layer=above, the box covers the chart: ${changes["layer=above"]} changes in its area over 4 s`);
} finally {
  rmSync(copy, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserLayer: all checks passed" : `qa_BrowserLayer: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

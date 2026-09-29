// A reveal step may say how its blocks arrive: `:::step {in=fade}` brings them up from nothing, `{in=rise}` also
// lifts them into place from a little below. Measured on the paragraph the step adds, while it arrives: under fade
// its ink passes through intermediate brightness on its way to full; under rise its top edge moves up to where it
// settles. Each reveal is stretched to two seconds so a screenshot catches it in flight. Stepping back is at once.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt } from "../devtools/png.mjs";
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserReveal.mjs <viewer-web directory>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const kRight = ["ArrowRight", "ArrowRight", 39];
const kLeft = ["ArrowLeft", "ArrowLeft", 37];
const kStepsColumn = 640; // right of the steps slide's syntax panel (it ends near x 610), where its steps arrive

/// the brightest ink and the first inked row below `from`, right of the menu
const inkBelow = (image, from) => {
  let brightest = 0;
  let firstRow = -1;
  for (let y = from; y < image.height - 60; ++y) {
    for (let x = kStepsColumn; x < image.width - 40; x += 2) {
      const pixel = pixelAt(image, x, y);
      const level = pixel.r + pixel.g + pixel.b;
      brightest = Math.max(brightest, level);
      if (level > 150 && firstRow < 0) {
        firstRow = y;
      }
    }
  }
  return { brightest, firstRow };
};

const copy = mkdtempSync(join(tmpdir(), "reveal-"));
try {
  cpSync(directory, copy, { recursive: true });
  const deck = join(copy, "default", "talk.md");
  const text = readFileSync(deck, "utf8").replace(":::step {in=fade}\n\nFades in.", ":::step {in=fade dur=2}\n\nFades in.").replace(":::step {in=rise from=left}\n\nRises", ":::step {in=rise dur=2}\n\nRises"); // from below, the default, which this measures
  if (!text.includes(":::step {in=fade dur=2}\n\nFades in.") || !text.includes(":::step {in=rise dur=2}\n\nRises")) throw new Error("the steps slide's fade and rise are not where this test expects them");
  writeFileSync(deck, text);

  await withViewer(copy, async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html#view=steps&step=0`);
    await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
    await page.waitFor("document.getElementById('canvas') && window.gr4Zoom && (globalThis.gr4Location || '').includes('view=steps') ? 1 : false");
    await page.evaluate("document.getElementById('canvas').focus()");
    let base = null;
    for (let settle = 0; settle < 60; ++settle) {
      if (settle % 5 === 0) await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 1100 + (settle % 10), y: 600 });
      base = (await page.command("Page.captureScreenshot", { format: "png" })).result.data;
    }
    // the rows the first paragraph uses end where the step's paragraph will begin
    const before = decodePng(Buffer.from(base, "base64"));
    let below = 0;
    for (let y = 60; y < before.height - 60; ++y) {
      for (let x = kStepsColumn; x < before.width - 40; x += 2) {
        const pixel = pixelAt(before, x, y);
        if (pixel.r + pixel.g + pixel.b > 150) below = y;
      }
    }
    const sample = async (from) => {
      const pressed = Date.now();
      await page.press(...kRight);
      const samples = [];
      while (Date.now() - pressed < 2800) {
        const shot = (await page.command("Page.captureScreenshot", { format: "png" })).result.data;
        samples.push({ at: Date.now() - pressed, ink: inkBelow(decodePng(Buffer.from(shot, "base64")), from) });
      }
      return samples;
    };

    const fading = await sample(below + 4);
    const full = fading[fading.length - 1].ink.brightest;
    const partway = fading.filter((each) => each.at > 300 && each.at < 1700 && each.ink.brightest > full * 0.15 && each.ink.brightest < full * 0.85);
    expect(full > 300 && partway.length >= 2, `{in=fade}: the paragraph passes through ${partway.length} frames of partial brightness on its way to ${full}`);

    // the second step's paragraph lies below the first step's
    const shotNow = decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
    let below2 = below;
    for (let y = below + 4; y < shotNow.height - 60; ++y) {
      for (let x = kStepsColumn; x < shotNow.width - 40; x += 2) {
        const pixel = pixelAt(shotNow, x, y);
        if (pixel.r + pixel.g + pixel.b > 150) below2 = y;
      }
    }
    const rising = await sample(below2 + 4);
    const settledTop = rising[rising.length - 1].ink.firstRow;
    const lower = rising.filter((each) => each.at > 300 && each.at < 1700 && each.ink.firstRow > settledTop + 2);
    expect(settledTop > 0 && lower.length >= 2, `{in=rise}: the paragraph starts below its place and rises to row ${settledTop}, ${lower.length} frames lower on the way`);

    // stepping back is at once: no frame of the reverse shows partial brightness
    const pressed = Date.now();
    await page.press(...kLeft);
    await page.waitFor("(globalThis.gr4Location || '').includes('step=1') ? 1 : false");
    const back = inkBelow(decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64")), below2 + 4);
    expect(back.firstRow < 0, `stepping back hides the step's paragraph at once, ${Date.now() - pressed} ms`);
    await page.close();
  });
  // a box arrives with its slide when the slide is entered going forward: the grid slide's box a fades in
  const boxDeck = readFileSync(deck, "utf8").replace(":::a {frame size=85%}\n**a** and", ":::a {frame size=85% in=fade dur=2}\n**a** and");
  if (boxDeck === readFileSync(deck, "utf8")) throw new Error("the grid slide's box a is not where this test expects it");
  writeFileSync(deck, boxDeck);
  await withViewer(copy, async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html#view=steps&step=0`);
    await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
    // the link's own fragment is there before the deck has loaded, so the deck's published state is waited for instead
    await page.waitFor("document.getElementById('canvas') && window.gr4Zoom && (globalThis.gr4Location || '').includes('view=steps') ? 1 : false");
    await page.evaluate("document.getElementById('canvas').focus()");
    for (let settle = 0; settle < 30; ++settle) {
      await page.command("Page.captureScreenshot", { format: "png" });
    }
    for (let press = 0; press < 30 && !(await page.evaluate("(globalThis.gr4Location || '')")).includes("view=placed"); ++press) {
      const before = await page.evaluate("(globalThis.gr4Location || '')");
      await page.press(...kRight);
      await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? 1 : false`);
      if ((await page.evaluate("(globalThis.gr4Location || '')")).includes("view=grid")) {
        // box a's first lines, between the syntax panel and box b
        const samples = [];
        const entered = Date.now();
        while (Date.now() - entered < 2600) {
          const image = decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
          let brightest = 0;
          for (let y = 120; y < 260; ++y) {
            for (let x = 560; x < 840; x += 2) {
              const pixel = pixelAt(image, x, y);
              brightest = Math.max(brightest, pixel.r + pixel.g + pixel.b);
            }
          }
          samples.push({ at: Date.now() - entered, brightest });
        }
        const full = samples[samples.length - 1].brightest;
        const partway = samples.filter((each) => each.at > 400 && each.at < 1700 && each.brightest > full * 0.15 && each.brightest < full * 0.85);
        expect(full > 300 && partway.length >= 2, `a box with {in=fade} arrives with its slide, ${partway.length} frames of partial brightness on the way to ${full}`);
        break;
      }
    }
    await page.close();
  });

  // `after=` counts from when the step before has finished arriving: with a wipe of 1.5 s and `after=1`, the deck
  // moves on by itself 2.5 s after the wipe's step is reached, not 1 s after (the rule as the author decided it)
  // the step right after the wipe is made the timed one; the deck's own timed step comes later
  const timedDeck = readFileSync(deck, "utf8").replace(":::step {in=wipe from=above dur=0.8}\n\n- wiped", ":::step {in=wipe dur=1.5}\n\n- wiped").replace(":::step {in=grow}\n\nGrows", ":::step {after=1}\n\nGrows");
  if (!timedDeck.includes("{in=wipe dur=1.5}\n\n- wiped") || !timedDeck.includes("{after=1}\n\nGrows")) throw new Error("the steps slide's wipe and timed step are not where this test expects them");
  writeFileSync(deck, timedDeck);
  await withViewer(copy, async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html#view=steps&step=2`);
    await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
    await page.waitFor("document.getElementById('canvas') && window.gr4Zoom && (globalThis.gr4Location || '').includes('view=steps&step=2') ? 1 : false");
    await page.evaluate("document.getElementById('canvas').focus()");
    await page.press(...kRight);
    await page.waitFor("(globalThis.gr4Location || '').includes('step=3') ? 1 : false");
    const reached = Date.now();
    await page.waitFor("(globalThis.gr4Location || '').includes('step=4') ? 1 : false", 10000);
    const waited = (Date.now() - reached) / 1000;
    expect(waited >= 2.4 && waited < 3.5, `the timed step arrives 1 s after the 1.5 s wipe has finished: after ${waited.toFixed(2)} s`);
    await page.close();
  });
} finally {
  rmSync(copy, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserReveal: all checks passed" : `qa_BrowserReveal: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

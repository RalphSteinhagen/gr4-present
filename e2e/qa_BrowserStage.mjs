// A `:::stage` on the transitions slide: each step plays the next transition between two of the deck's slides,
// drawn small in a box, and the code line of that step is picked out on the left (`{follow}`).
//
// Ground truth is what each picture is, not how it is drawn: at step 0 the box shows the `from` slide; while a step's
// transition runs, the box is neither slide; once it has run, the box is the `to` slide, as the same deck with `from`
// and `to` swapped shows it at step 0. The highlighted line moves down as the steps advance: from step 1 to step 2 the
// rows that dim lie above the rows that light up.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng } from "../devtools/png.mjs";
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserStage.mjs <viewer-web directory>");
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
const kWidth = 1280;
const kHeight = 720;
const kBox = { x: Math.round(kWidth * 0.46), y: 200, width: Math.round(kWidth * 0.48), height: 340 }; // inside the stage
const kCode = { x: 70, y: 225, width: 420, height: 330 }; // inside the code panel

const shot = async (page) => decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));

const differs = (a, b, box) => {
  let changed = 0;
  let all = 0;
  for (let y = box.y; y < box.y + box.height; y += 3) {
    for (let x = box.x; x < box.x + box.width; x += 3) {
      const i = (y * a.width + x) * 4;
      const d = Math.abs(a.rgba[i] - b.rgba[i]) + Math.abs(a.rgba[i + 1] - b.rgba[i + 1]) + Math.abs(a.rgba[i + 2] - b.rgba[i + 2]);
      changed += d > 24 ? 1 : 0;
      ++all;
    }
  }
  return changed / all;
};

/// per row of `box`, the summed brightness
const rowLight = (image, box) => {
  const rows = [];
  for (let y = box.y; y < box.y + box.height; ++y) {
    let sum = 0;
    for (let x = box.x; x < box.x + box.width; ++x) {
      const i = (y * image.width + x) * 4;
      sum += image.rgba[i] + image.rgba[i + 1] + image.rgba[i + 2];
    }
    rows.push(sum);
  }
  return rows;
};

const opened = async (browser, server, step) => {
  const page = await openPage(browser, `${server.origin}/index.html#view=transitions&step=${step}`);
  await page.command("Emulation.setDeviceMetricsOverride", { width: kWidth, height: kHeight, deviceScaleFactor: 1, mobile: false });
  await page.waitFor("(globalThis.gr4Location || '').includes('view=transitions') && window.gr4Zoom ? 1 : false");
  await page.evaluate("document.getElementById('canvas').focus()");
  let image = null;
  for (let frame = 0; frame < 30; ++frame) {
    image = await shot(page);
  }
  return { page, image };
};

const copy = mkdtempSync(join(tmpdir(), "stage-"));
try {
  cpSync(directory, copy, { recursive: true });
  const deck = join(copy, "default", "talk.md");
  const original = readFileSync(deck, "utf8");
  const kPair = "from: markdown\nto: solvay";
  if (!original.includes(kPair)) throw new Error("the stage is not where this test expects it");

  // the `to` slide as the stage shows it at rest
  writeFileSync(deck, original.replace(kPair, "from: solvay\nto: markdown"));
  let arrivedTruth = null;
  await withViewer(copy, async ({ server, browser }) => {
    const { page, image } = await opened(browser, server, 0);
    arrivedTruth = image;
    await page.close();
  });
  writeFileSync(deck, original);

  await withViewer(copy, async ({ server, browser }) => {
    const { page, image: before } = await opened(browser, server, 0);
    expect(differs(before, arrivedTruth, kBox) > 0.05, `step 0 shows the from slide, ${(100 * differs(before, arrivedTruth, kBox)).toFixed(1)} % of the box apart from the to slide`);

    for (const [step, name] of [[1, "push-up"], [8, "crt"]]) {
      while ((await page.evaluate("globalThis.gr4Location || ''")).match(/step=(\d+)/)?.[1] !== String(step)) {
        await page.press(...kRight);
        await shot(page);
      }
      const started = Date.now();
      const frames = [];
      while (Date.now() - started < 2600) {
        frames.push({ at: Date.now() - started, image: await shot(page) });
      }
      const between = frames.filter(({ image }) => differs(image, before, kBox) > 0.01 && differs(image, arrivedTruth, kBox) > 0.01);
      const arrived = frames.filter(({ image }) => differs(image, arrivedTruth, kBox) < 0.01);
      expect(between.length >= 2, `${name}: in flight the box is neither slide, ${between.length} of ${frames.length} frames`);
      expect(arrived.length >= 1, `${name}: and then the to slide, ${arrived.length} frames at rest on it`);
    }
    expect(page.consoleLines.filter((line) => /stage|shader.*(error|fail)/i.test(line)).length === 0, "the stage builds and reports nothing");
    await page.close();

    const one = await opened(browser, server, 1);
    const two = await opened(browser, server, 2);
    const delta = rowLight(one.image, kCode).map((light, row) => light - rowLight(two.image, kCode)[row]);
    const brighterAtOne = delta.flatMap((d, row) => (d > 2000 ? [row] : []));
    const brighterAtTwo = delta.flatMap((d, row) => (d < -2000 ? [row] : []));
    expect(brighterAtOne.length > 3 && brighterAtTwo.length > 3 && Math.max(...brighterAtOne) < Math.min(...brighterAtTwo), `the highlighted line moves down a line from step 1 to 2: rows ${brighterAtOne[0]}..${brighterAtOne.at(-1)} then ${brighterAtTwo[0]}..${brighterAtTwo.at(-1)}`);
    await one.page.close();
    await two.page.close();
  });
} finally {
  rmSync(copy, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserStage: all checks passed" : `qa_BrowserStage: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

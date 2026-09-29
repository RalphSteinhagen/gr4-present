// `rotate:` turns the scenery about the middle of its room while the words stay upright.
// Ground truth: a drawing turned by 180 degrees is the unturned drawing point-mirrored about its middle.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt } from "../devtools/png.mjs";
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserRotation.mjs <viewer-web directory>");
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

const capture = async (deckDirectory, sampleMove) => {
  let result = null;
  await withViewer(deckDirectory, async ({ server, browser }) => {
    const page = await openPage(browser, "about:blank");
    await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
    await page.command("Page.navigate", { url: `${server.origin}/index.html#view=ring&step=0` });
    await page.waitFor("document.getElementById('canvas') && window.gr4Zoom && (globalThis.gr4Location || '').includes('view=ring') ? 1 : false");
    await page.evaluate("document.getElementById('canvas').focus()");
    for (let settle = 0; settle < 40; ++settle) {
      if (settle % 5 === 0) await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 1100 + (settle % 10), y: 600 });
      await page.command("Page.captureScreenshot", { format: "png" });
    }
    const pressed = Date.now();
    await page.press(...kRight);
    const turning = [];
    while (sampleMove && Date.now() - pressed < 1500) {
      turning.push(Number(await page.evaluate("window.gr4CameraRotation || 0")));
    }
    let shot = "";
    for (let frame = 0; frame < 60; ++frame) {
      if (frame % 5 === 0) await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 1100 + (frame % 10), y: 600 });
      shot = (await page.command("Page.captureScreenshot", { format: "png" })).result.data;
    }
    result = { image: decodePng(Buffer.from(shot, "base64")), turning, rotation: Number(await page.evaluate("window.gr4CameraRotation || 0")) };
    await page.close();
  });
  return result;
};

const copy = mkdtempSync(join(tmpdir(), "rotation-"));
try {
  cpSync(directory, copy, { recursive: true });
  const deck = join(copy, "default", "talk.md");
  const text = readFileSync(deck, "utf8");
  const experiment = text.indexOf("{#experiment}");
  const turnedTo = (layout) => text.slice(0, experiment) + text.slice(experiment).replace("rotate: 90\n", layout);
  if (turnedTo("") === text) throw new Error("the experiment slide's rotate: 90 is not where this test expects it");
  writeFileSync(deck, turnedTo(""));
  const plain = await capture(copy, false);
  writeFileSync(deck, turnedTo("rotate: 180\n"));
  const turned = await capture(copy, true);

  expect(turned.rotation === 180 && plain.rotation === 0, `the camera reports its turn, ${plain.rotation} and ${turned.rotation} degrees`);
  const between = turned.turning.filter((degrees) => degrees > 5 && degrees < 175).length;
  expect(between >= 3, `the move into the turned stop turns gradually, ${between} samples between 0 and 180 degrees`);

  const { width, height } = plain.image;
  const mismatch = (cx2, cy2) => {
    let total = 0;
    let count = 0;
    for (let y = Math.floor(height * 0.3); y < height * 0.7; y += 4) {
      for (let x = Math.floor(width * 0.3); x < width * 0.7; x += 4) {
        const mirrored = pixelAt(plain.image, cx2 - x, cy2 - y);
        const here = pixelAt(turned.image, x, y);
        total += Math.abs(mirrored.r - here.r) + Math.abs(mirrored.g - here.g) + Math.abs(mirrored.b - here.b);
        ++count;
      }
    }
    return total / count / 3;
  };
  let best = { difference: Infinity, cx2: 0, cy2: 0 };
  for (let cy2 = Math.floor(height * 0.8); cy2 <= Math.ceil(height * 1.2); ++cy2) {
    for (const cx2 of [width - 2, width - 1, width]) {
      const difference = mismatch(cx2, cy2);
      if (difference < best.difference) best = { difference, cx2, cy2 };
    }
  }
  let unturned = 0;
  let count = 0;
  for (let y = Math.floor(height * 0.3); y < height * 0.7; y += 4) {
    for (let x = Math.floor(width * 0.3); x < width * 0.7; x += 4) {
      const a = pixelAt(plain.image, x, y);
      const b = pixelAt(turned.image, x, y);
      unturned += (Math.abs(a.r - b.r) + Math.abs(a.g - b.g) + Math.abs(a.b - b.b)) / 3;
      ++count;
    }
  }
  unturned /= count;
  expect(
    best.difference < 6 && unturned > best.difference * 3,
    `turned by 180 degrees the drawing is its own point mirror about (${best.cx2 / 2}, ${best.cy2 / 2}), ${best.difference.toFixed(1)} levels off, against ${unturned.toFixed(1)} unturned`,
  );
} finally {
  rmSync(copy, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserRotation: all checks passed" : `qa_BrowserRotation: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

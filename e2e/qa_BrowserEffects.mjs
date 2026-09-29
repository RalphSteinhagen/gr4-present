// The effect shaders a slide uses other than as a transition: a background, a viewport, an overlay, a reveal, the
// pointer and the break screen, each on the default deck's effects slides.
//
// Ground truth is what each use is for, never how it is drawn: a background and a viewport move while nothing else on
// the slide does; an overlay changes the box it covers, against the same deck without it; a reveal is neither step
// while it runs and then exactly the step a page opened on it shows; the laser pointer is red where the pointer is,
// and only while it is on; the break screen covers the slide and any navigation key takes it away without moving.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng } from "../devtools/png.mjs";
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserEffects.mjs <viewer-web directory>");
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

const shot = async (page) => decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));

/// the share of sampled pixels inside `box` ({x, y, width, height}, CSS pixels) that differ by more than a few levels
const differs = (a, b, box = { x: 340, y: 0, width: kWidth - 340, height: kHeight }) => {
  let changed = 0;
  let all = 0;
  for (let y = box.y; y < box.y + box.height; y += 5) {
    for (let x = box.x; x < box.x + box.width; x += 5) {
      const i = (y * a.width + x) * 4;
      const d = Math.abs(a.rgba[i] - b.rgba[i]) + Math.abs(a.rgba[i + 1] - b.rgba[i + 1]) + Math.abs(a.rgba[i + 2] - b.rgba[i + 2]);
      changed += d > 24 ? 1 : 0;
      ++all;
    }
  }
  return changed / all;
};

/// pixels of a red laser dot near (cx, cy): red high, green and blue low
const redNear = (image, cx, cy, reach = 24) => {
  let red = 0;
  for (let y = cy - reach; y <= cy + reach; ++y) {
    for (let x = cx - reach; x <= cx + reach; ++x) {
      const i = (y * image.width + x) * 4;
      red += image.rgba[i] > 180 && image.rgba[i + 1] < 110 && image.rgba[i + 2] < 110 ? 1 : 0;
    }
  }
  return red;
};

/// a page on `view` at `step`, the viewer settled; `query` is the page's own, `?frozen` for effects at their still time
const opened = async (browser, server, view, step = 0, query = "") => {
  const page = await openPage(browser, `${server.origin}/index.html${query}#view=${view}&step=${step}`);
  await page.command("Emulation.setDeviceMetricsOverride", { width: kWidth, height: kHeight, deviceScaleFactor: 1, mobile: false });
  await page.waitFor(`(globalThis.gr4Location || '').includes('view=${view}') && window.gr4Zoom ? 1 : false`);
  await page.evaluate("document.getElementById('canvas').focus()");
  let image = null;
  for (let frame = 0; frame < 30; ++frame) {
    image = await shot(page);
  }
  return { page, image };
};

/// an effect that does not compile or cannot be read is reported in the console; one that is merely slow is not a failure
const effectComplaints = (page) => page.consoleLines.filter((line) => /effect shader cannot|shader.*(error|fail)|framebuffer/i.test(line));

const copy = mkdtempSync(join(tmpdir(), "effects-"));
try {
  cpSync(directory, copy, { recursive: true });
  const deck = join(copy, "default", "talk.md");
  const original = readFileSync(deck, "utf8");

  await withViewer(copy, async ({ server, browser }) => {
    // a background moves behind words that stay where they are
    {
      const { page, image } = await opened(browser, server, "effect-background");
      const later = await new Promise((resolve) => setTimeout(async () => resolve(await shot(page)), 1500));
      expect(differs(image, later) > 0.02, `background: moves, ${(100 * differs(image, later)).toFixed(2)} % of pixels changed in 1.5 s`);
      expect(effectComplaints(page).length === 0, `background: builds: ${effectComplaints(page).slice(0, 2).join(" | ") || "no complaint"}`);
      await page.close();
    }

    // a viewport moves inside its box
    {
      const { page, image } = await opened(browser, server, "effect-viewport");
      const later = await new Promise((resolve) => setTimeout(async () => resolve(await shot(page)), 1500));
      const right = { x: Math.round(kWidth * 0.55), y: 120, width: Math.round(kWidth * 0.4), height: 500 };
      expect(differs(image, later, right) > 0.05, `viewport: moves, ${(100 * differs(image, later, right)).toFixed(2)} % of its pixels changed in 1.5 s`);
      expect(effectComplaints(page).length === 0, `viewport: builds: ${effectComplaints(page).slice(0, 2).join(" | ") || "no complaint"}`);
      await page.close();
    }

    // the reveal is neither step while it runs, then the step a page opened on it shows
    {
      const truth = await opened(browser, server, "effect-reveal", 1);
      const settled = truth.image;
      await truth.page.close();
      const { page, image: before } = await opened(browser, server, "effect-reveal", 0);
      const pressed = Date.now();
      await page.press(...kRight);
      const inFlight = [];
      while (Date.now() - pressed < 1400) {
        const image = await shot(page);
        if (Date.now() - pressed > 200) {
          inFlight.push(image);
        }
      }
      let after = null;
      for (let frame = 0; frame < 40; ++frame) {
        after = await shot(page);
      }
      const between = inFlight.filter((image) => differs(image, before) > 0.002 && differs(image, after) > 0.002);
      expect(between.length >= 2, `reveal: in flight neither step, ${between.length} of ${inFlight.length} frames`);
      expect(differs(after, settled) < 0.01, `reveal: and then the step itself, ${(100 * differs(after, settled)).toFixed(2)} % of pixels differ`);
      expect(effectComplaints(page).length === 0, `reveal: builds: ${effectComplaints(page).slice(0, 2).join(" | ") || "no complaint"}`);
      await page.close();
    }

    // L: a red dot where the pointer is, only while pointer mode is on
    {
      const { page } = await opened(browser, server, "effect-pointer");
      const kAt = [900, 500];
      const move = async () => page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: kAt[0], y: kAt[1] });
      await move();
      const without = await shot(page);
      await page.press("l", "KeyL", 76);
      let pointing = null;
      for (let frame = 0; frame < 20; ++frame) {
        await move();
        pointing = await shot(page);
      }
      expect(redNear(pointing, ...kAt) > 20 + redNear(without, ...kAt), `pointer: a red dot under the pointer, ${redNear(pointing, ...kAt)} red pixels against ${redNear(without, ...kAt)} before`);
      await page.press("l", "KeyL", 76);
      let off = null;
      for (let frame = 0; frame < 20; ++frame) {
        off = await shot(page);
      }
      expect(redNear(off, ...kAt) <= redNear(without, ...kAt) + 2, `pointer: gone once L is pressed again, ${redNear(off, ...kAt)} red pixels`);
      expect(effectComplaints(page).length === 0, `pointer: builds: ${effectComplaints(page).slice(0, 2).join(" | ") || "no complaint"}`);

      // B: the break screen covers the slide; a navigation key ends it where the talk was
      const slide = await shot(page);
      await page.press("b", "KeyB", 66);
      let onBreak = null;
      for (let frame = 0; frame < 20; ++frame) {
        onBreak = await shot(page);
      }
      const whole = { x: 0, y: 0, width: kWidth, height: kHeight };
      expect(differs(slide, onBreak, whole) > 0.3, `break: covers the slide, ${(100 * differs(slide, onBreak, whole)).toFixed(1)} % of pixels changed`);
      await page.press(...kRight);
      let back = null;
      for (let frame = 0; frame < 30; ++frame) {
        back = await shot(page);
      }
      const location = await page.evaluate("globalThis.gr4Location || ''");
      expect(location.includes("view=effect-pointer"), `break: a navigation key ends it without moving, at ${location}`);
      expect(differs(slide, back, whole) < 0.01, `break: and the slide is as it was, ${(100 * differs(slide, back, whole)).toFixed(2)} % of pixels differ`);
      await page.close();
    }
  });

  // a background the display cannot keep up with gives up resolution and says so, once (D26, D39): the CPU is
  // throttled so even a fast machine's software GL falls behind
  await withViewer(copy, async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html#view=effect-background&step=0`);
    await page.command("Emulation.setDeviceMetricsOverride", { width: kWidth, height: kHeight, deviceScaleFactor: 1, mobile: false });
    await page.waitFor("(globalThis.gr4Location || '').includes('view=effect-background') && window.gr4Zoom ? 1 : false");
    await page.command("Emulation.setCPUThrottlingRate", { rate: 6 });
    const started = Date.now();
    while (Date.now() - started < 8000 && !page.consoleLines.some((line) => line.includes("too slow for the display"))) {
      await shot(page);
    }
    const said = page.consoleLines.filter((line) => line.includes("too slow for the display"));
    expect(said.length >= 1, `a slow background halves its resolution and says so: ${said[0] || "nothing said"}`);
    await page.command("Emulation.setCPUThrottlingRate", { rate: 1 });
    await page.close();
  });

  // an overlay changes the box it covers, against the same deck without it
  const kOverlaid = "{overlay=scanlines overlay.strength=1 overlay.curvature=0.3}";
  if (!original.includes(`:::content ${kOverlaid}`)) throw new Error("the overlay slide's box is not where this test expects it");
  const box = { x: Math.round(kWidth * 0.5), y: 150, width: Math.round(kWidth * 0.45), height: 450 };
  let overlaid = null;
  await withViewer(copy, async ({ server, browser }) => {
    const { page, image } = await opened(browser, server, "effect-overlay");
    overlaid = image;
    expect(effectComplaints(page).length === 0, `overlay: builds: ${effectComplaints(page).slice(0, 2).join(" | ") || "no complaint"}`);
    await page.close();
  });
  writeFileSync(deck, original.replace(`:::content ${kOverlaid}`, ":::content"));
  await withViewer(copy, async ({ server, browser }) => {
    const { page, image } = await opened(browser, server, "effect-overlay");
    expect(differs(overlaid, image, box) > 0.05, `overlay: changes the box it covers, ${(100 * differs(overlaid, image, box)).toFixed(2)} % of its pixels`);
    await page.close();
  });

  // a transition the deck and the viewer lack arrives as the default move, said once in the console; a background
  // reached through a shader transition, whose capture nests the background's own, settles to the frame a page
  // opened on it shows -- both effects at their still time, so the two pages draw the same frame
  const kBackgroundLayout = "background: starnest";
  if (!original.includes(kBackgroundLayout)) throw new Error("the background slide's layout is not where this test expects it");
  const kMarkdownGrid = "grid: [(syntax, 0.45), (content)]";
  const at = original.indexOf(kMarkdownGrid, original.indexOf("{#markdown}"));
  if (at < 0) throw new Error("the markdown slide's layout is not where this test expects it");
  const withTransitions = original.slice(0, at) + `${kMarkdownGrid}\ntransition: nosuch\nduration: 0.5` + original.slice(at + kMarkdownGrid.length);
  writeFileSync(deck, withTransitions.replace(kBackgroundLayout, `${kBackgroundLayout}\ntransition: crt\nduration: 0.5`));
  await withViewer(copy, async ({ server, browser }) => {
    const truth = await opened(browser, server, "markdown");
    const { page } = await opened(browser, server, "intro");
    await page.press(...kRight);
    let after = null;
    for (let frame = 0; frame < 40; ++frame) {
      after = await shot(page);
    }
    expect(differs(after, truth.image) < 0.01, `an unknown transition arrives as the default move, ${(100 * differs(after, truth.image)).toFixed(2)} % of pixels differ`);
    const said = page.consoleLines.filter((line) => line.includes("nosuch"));
    expect(said.length >= 1, `and is reported: ${said[0] || "nothing said"}`);
    await truth.page.close();
    await page.close();

    const background = await opened(browser, server, "effect-background", 0, "?frozen");
    const through = await opened(browser, server, "effects", 0, "?frozen");
    await through.page.press(...kRight);
    let arrived = null;
    for (let frame = 0; frame < 40; ++frame) {
      arrived = await shot(through.page);
    }
    expect(differs(arrived, background.image) < 0.01, `a background reached through crt settles to the frame a page opened on it shows, ${(100 * differs(arrived, background.image)).toFixed(2)} % of pixels differ`);
    expect(effectComplaints(through.page).length === 0, `background through a transition builds: ${effectComplaints(through.page).slice(0, 2).join(" | ") || "no complaint"}`);
    await background.page.close();
    await through.page.close();
  });

  // a zoom seen through an effect: the test effect inverts the picture, so while the move runs the dark slides turn
  // bright, which neither is; once arrived, the slide is as a page opened on it shows it (D33)
  writeFileSync(join(copy, "default", "effects", "invert.glsl"), "void mainImage(out vec4 colour, in vec2 fragCoord) { colour = vec4(1.0 - texture(iContent, fragCoord / iResolution.xy).rgb, 1.0); }\n");
  writeFileSync(deck, original.slice(0, at) + `${kMarkdownGrid}\ntransition: zoom invert\nduration: 2` + original.slice(at + kMarkdownGrid.length));
  await withViewer(copy, async ({ server, browser }) => {
    const truth = await opened(browser, server, "markdown");
    const { page } = await opened(browser, server, "intro");
    const meanLight = (image) => {
      let sum = 0;
      let all = 0;
      for (let i = 0; i < image.rgba.length; i += 4 * 97) {
        sum += image.rgba[i] + image.rgba[i + 1] + image.rgba[i + 2];
        ++all;
      }
      return sum / (3 * all);
    };
    await page.press(...kRight);
    const started = Date.now();
    let brightest = 0;
    while (Date.now() - started < 1800) {
      brightest = Math.max(brightest, meanLight(await shot(page)));
    }
    let after = null;
    for (let frame = 0; frame < 40; ++frame) {
      after = await shot(page);
    }
    expect(brightest > 128 && meanLight(truth.image) < 64, `zoom through an effect: the moving picture is inverted, mean light ${brightest.toFixed(0)} against ${meanLight(truth.image).toFixed(0)} at rest`);
    expect(differs(after, truth.image) < 0.01, `and then the slide itself, ${(100 * differs(after, truth.image)).toFixed(2)} % of pixels differ`);
    await truth.page.close();
    await page.close();
  });
} finally {
  rmSync(copy, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserEffects: all checks passed" : `qa_BrowserEffects: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

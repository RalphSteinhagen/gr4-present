// `transition: push` slides the new slide in from the side the deck moves towards and pushes the old one out the
// other side; `transition: zoom` grows the old slide away while the new one grows from slightly smaller into place.
//
// Measured on the arriving slide's heading, whose ink is found in each frame as the deck moves from the first slide
// to the second: under push its right edge comes in from the right of the screen and settles where it belongs; under
// zoom the heading grows into place about the screen's centre, so its right edge comes in from nearer the middle.
// Each transition is stretched to two seconds so a screenshot catches it in flight.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt } from "../devtools/png.mjs";
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserTransitions.mjs <viewer-web directory>");
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
const isLit = (pixel) => pixel.r + pixel.g + pixel.b > 200;

/// the heading's ink in the title band, by its right edge: the side menu, which a synthesised pointer does not reliably
/// close, covers the left 310 px of the band but never reaches the end of this heading
const kMenuEdge = 320;
const headingOf = (image) => {
  let right = -1;
  for (let y = 0; y < 70; ++y) {
    for (let x = kMenuEdge; x < image.width; ++x) {
      if (isLit(pixelAt(image, x, y))) {
        right = Math.max(right, x);
      }
    }
  }
  return { right };
};

/// how much of the frame right of the menu is ink, which under a fade through the background falls to almost nothing
const inkShare = (image) => {
  let lit = 0;
  let all = 0;
  for (let y = 0; y < image.height; y += 3) {
    for (let x = kMenuEdge; x < image.width; x += 3) {
      lit += isLit(pixelAt(image, x, y)) ? 1 : 0;
      ++all;
    }
  }
  return lit / all;
};

for (const kind of ["push", "zoom", "push-down", "fade-through"]) {
  const copy = mkdtempSync(join(tmpdir(), `transition-${kind}-`));
  try {
    cpSync(directory, copy, { recursive: true });
    const deck = join(copy, "default", "talk.md");
    const text = readFileSync(deck, "utf8");
    const second = text.indexOf("\n", text.indexOf("{#markdown}"));
    writeFileSync(deck, text.slice(0, second + 1) + `\n:::layout\ntransition: ${kind}\nduration: 2\n:::\n` + text.slice(second + 1));

    await withViewer(copy, async ({ server, browser }) => {
      const page = await openPage(browser, `${server.origin}/index.html`);
      await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
      await page.waitFor("(globalThis.gr4Location || '').includes('view=intro') && (globalThis.gr4Location || '').includes('step=0')");
      await page.evaluate("document.getElementById('canvas').focus()");
      // the side menu opens at the left edge and a synthesised pointer does not reliably leave it, so the pointer is
      // moved away again and again until the menu has closed over the heading's columns
      for (let settle = 0; settle < 60; ++settle) {
        if (settle % 5 === 0) {
          await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 1100 + (settle % 10), y: 600 });
        }
        await page.command("Page.captureScreenshot", { format: "png" });
      }
      const resting = decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
      const pressed = Date.now();
      await page.press(...kRight);
      const samples = [];
      while (Date.now() - pressed < 3200) {
        const shot = (await page.command("Page.captureScreenshot", { format: "png" })).result.data;
        const image = decodePng(Buffer.from(shot, "base64"));
        samples.push({ at: Date.now() - pressed, heading: headingOf(image), ink: inkShare(image), image: kind === "push-down" ? image : null });
      }
      const settled = samples[samples.length - 1].heading;
      const inFlight = samples.filter((each) => each.at > 300 && each.at < 1700);
      if (kind === "push") {
        const travelling = inFlight.filter((each) => each.heading.right > settled.right + 40);
        expect(travelling.length >= 2, `push: the arriving heading comes in from the right, ${travelling.length} frames of it right of where it settles at ${settled.right}`);
        // a heading still entering is cut by the screen's edge, so its visible right edge is the screen's, wavering
        // with whichever glyph happens to be cut there; only a heading wholly on screen has a right edge of its own
        const kEnteringMargin = 24; // px: wider than a glyph's own gaps at the 36 pt title size
        // the leaving slide's own heading line is pushed out to the left and may still be the band's widest ink in the
        // first frames, so the arriving heading is followed from the frame it is furthest right, which is where it enters
        const seen = inFlight.map((each) => each.heading.right);
        const arrived = seen.indexOf(Math.max(...seen));
        const rights = seen.slice(Math.max(0, arrived)).filter((right) => right > settled.right + 2 && right < 1280 - kEnteringMargin);
        expect(rights.length >= 4, `push: the heading is seen wholly on screen while still moving in ${rights.length} frames`);
        expect(rights.every((right, index) => index === 0 || right <= rights[index - 1] + 2), `push: and moves one way only, ${JSON.stringify(rights)}`);
      } else if (kind === "push-down") {
        // the leaving slide, moved by (dx, dy), against a frame in flight: the mean difference over what of it is still
        // on screen. Under a push down the best match is straight down, and no sideways shift matches as well.
        const mismatch = (image, dx, dy) => {
          let sum = 0;
          let count = 0;
          for (let y = Math.max(0, dy); y < image.height && y - dy < image.height; y += 4) {
            for (let x = Math.max(340, dx); x < image.width && x - dx < image.width; x += 4) {
              const a = pixelAt(image, x, y);
              const b = pixelAt(resting, x - dx, y - dy);
              sum += Math.abs(a.r - b.r) + Math.abs(a.g - b.g) + Math.abs(a.b - b.b);
              ++count;
            }
          }
          return count > 2000 ? sum / count : Infinity;
        };
        const middle = inFlight[Math.floor(inFlight.length / 2)].image;
        let down = { shift: 0, error: Infinity };
        for (let dy = 0; dy < 600; dy += 4) {
          const error = mismatch(middle, 0, dy);
          down = error < down.error ? { shift: dy, error } : down;
        }
        let sideways = Infinity;
        for (let dx = -600; dx <= 600; dx += 8) {
          sideways = dx === 0 ? sideways : Math.min(sideways, mismatch(middle, dx, 0));
        }
        expect(down.shift > 20 && down.error < sideways, `push-down: halfway the leaving slide has moved down by ${down.shift} px (mismatch ${down.error.toFixed(1)}), better than any sideways shift (${sideways.toFixed(1)})`);
      } else if (kind === "fade-through") {
        // the old slide is gone before the new one comes: halfway there is next to no ink at all
        const settledInk = samples[samples.length - 1].ink;
        const darkest = Math.min(...inFlight.map((each) => each.ink));
        expect(darkest < settledInk * 0.15, `fade-through: halfway the screen is all but empty, ${(100 * darkest).toFixed(2)} % ink against ${(100 * settledInk).toFixed(2)} %`);
      } else {
        // the arriving heading is a little smaller than it will be, scaled about the screen's centre, so its right edge
        // sits nearer the middle than where it settles
        const growing = inFlight.filter((each) => each.heading.right > 0 && Math.abs(each.heading.right - 640) < Math.abs(settled.right - 640) - 2);
        expect(growing.length >= 2, `zoom: the arriving heading grows into place, its right edge at ${JSON.stringify(inFlight.map((each) => each.heading.right))} before settling at ${settled.right}`);
      }
      await page.close();
    });
  } finally {
    rmSync(copy, { recursive: true, force: true });
  }
}

// `transition: morph`: a slide that repeats the intro's title and paragraph, the paragraph lower down. Ground truth
// is where each slide, settled, has the paragraph's first word: halfway through the move its ink is between the two,
// in rows where neither slide has any ink of its own.
{
  const copy = mkdtempSync(join(tmpdir(), "transition-morph-"));
  try {
    cpSync(directory, copy, { recursive: true });
    const deck = join(copy, "default", "talk.md");
    const text = readFileSync(deck, "utf8");
    const kParagraph = "Slides written in Markdown, laid out by SVG masters, with **GNU Radio 4** and OpenDigitizer widgets running live\ninside them, natively and in the browser.";
    if (!text.includes(kParagraph)) throw new Error("the intro's paragraph is not what this test expects");
    const slide = `# gr4-present<br>Markdown and SVG slides with live GNU Radio 4 signals {#again}\n\n:::layout\ntransition: morph\nduration: 2\n:::\n\nfirst filler line\n\nsecond filler line\n\nthird filler line\n\nfourth filler line\n\n${kParagraph}\n\n`;
    const next = text.indexOf("\n# ", text.indexOf("{#intro}"));
    writeFileSync(deck, text.slice(0, next + 1) + slide + text.slice(next + 1));
    await withViewer(copy, async ({ server, browser }) => {
      const settledRow = async (view) => {
        const page = await openPage(browser, `${server.origin}/index.html#view=${view}&step=0`);
        await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
        await page.waitFor(`(globalThis.gr4Location || '').includes('view=${view}&') && window.gr4TextRuns && window.gr4TextRuns.some((run) => run[4].startsWith('Slides')) ? 1 : false`);
        const runs = JSON.parse(await page.evaluate("JSON.stringify(window.gr4TextRuns)"));
        let image = null;
        for (let frame = 0; frame < 30; ++frame) {
          image = decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
        }
        await page.close();
        const word = runs.find((run) => run[4].startsWith("Slides"));
        return { y: word[1], height: word[3], x: word[0], image };
      };
      const from = await settledRow("intro");
      const to = await settledRow("again");
      expect(to.y > from.y + 60, `the paragraph is lower on the second slide, ${from.y} -> ${to.y}`);
      const page = await openPage(browser, `${server.origin}/index.html#view=intro&step=0`);
      await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
      await page.waitFor("(globalThis.gr4Location || '').includes('view=intro&') && window.gr4Zoom ? 1 : false");
      await page.evaluate("document.getElementById('canvas').focus()");
      await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 1100, y: 600 });
      for (let frame = 0; frame < 30; ++frame) await page.command("Page.captureScreenshot", { format: "png" });
      const inkIn = (image, y0, y1, x0, x1) => {
        let ink = 0;
        for (let y = Math.max(0, y0); y < Math.min(image.height, y1); ++y) for (let x = x0; x < x1; x += 2) ink += isLit(pixelAt(image, x, y)) ? 1 : 0;
        return ink;
      };
      const pressed = Date.now();
      await page.press(...kRight);
      let seenBetween = 0;
      const band = (y) => [Math.round(y), Math.round(y + from.height)];
      while (Date.now() - pressed < 1800) {
        const image = decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
        const at = Date.now() - pressed;
        if (at < 700 || at > 1300) continue;
        // rows a third and two thirds of the way between: ink there that neither settled slide has is the paragraph moving
        for (const share of [1 / 3, 1 / 2, 2 / 3]) {
          const [y0, y1] = band(from.y + share * (to.y - from.y));
          const x0 = Math.max(340, Math.round(from.x));
          const x1 = x0 + 300;
          if (inkIn(image, y0, y1, x0, x1) > 20 && inkIn(from.image, y0, y1, x0, x1) < 5 && inkIn(to.image, y0, y1, x0, x1) < 5) ++seenBetween;
        }
      }
      expect(seenBetween >= 1, `morph: the paragraph is seen between where it was and where it goes, ${seenBetween} times`);
      await page.close();
    });
  } finally {
    rmSync(copy, { recursive: true, force: true });
  }
}

console.log(failures.length === 0 ? "qa_BrowserTransitions: all checks passed" : `qa_BrowserTransitions: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

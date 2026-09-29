// The transitions a fragment shader draws from pictures of both slides: `transition: fire` and its kin. Each is
// stretched to two seconds and watched from the first slide to the second.
//
// Each runs three seconds, so even a loaded machine catches several frames of it in flight. Ground truth is what each effect is, not how it is computed: while it runs, the screen is neither the slide it left
// nor the one it goes to; once it has run, it is the slide it went to, as a page opened on that slide shows it; fire
// shows the colours of a flame -- red high, blue low -- which neither slide has; and the shader compiles, on WebGL 2.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng } from "../devtools/png.mjs";
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserShaderTransitions.mjs <viewer-web directory>");
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
const kEffects = ["fire", "fire2", "crt", "glitch", "waterfall", "sine", "disintegrate", "ripple", "curl", "cube", "flip"];

/// the share of sampled pixels that differ by more than a few levels between two pictures, right of the side menu
const differs = (a, b) => {
  let changed = 0;
  let all = 0;
  for (let y = 0; y < a.height; y += 6) {
    for (let x = 340; x < a.width; x += 6) {
      const i = (y * a.width + x) * 4;
      const d = Math.abs(a.rgba[i] - b.rgba[i]) + Math.abs(a.rgba[i + 1] - b.rgba[i + 1]) + Math.abs(a.rgba[i + 2] - b.rgba[i + 2]);
      changed += d > 24 ? 1 : 0;
      ++all;
    }
  }
  return changed / all;
};

/// pixels a flame has and a slide does not: red high, green in between, blue low
const flameShare = (image) => {
  let flame = 0;
  let all = 0;
  for (let y = 0; y < image.height; y += 4) {
    for (let x = 0; x < image.width; x += 4) {
      const i = (y * image.width + x) * 4;
      flame += image.rgba[i] > 200 && image.rgba[i + 1] > 60 && image.rgba[i + 1] < 210 && image.rgba[i + 2] < 90 ? 1 : 0;
      ++all;
    }
  }
  return flame / all;
};

const shot = async (page) => decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));

const copy = mkdtempSync(join(tmpdir(), "shader-transitions-"));
try {
  cpSync(directory, copy, { recursive: true });
  const deck = join(copy, "default", "talk.md");
  const original = readFileSync(deck, "utf8");
  // the second slide's own layout says how it is arrived at, so the slide itself stays as it is
  const kGrid = "grid: [(syntax, 0.45), (content)]";
  const at = original.indexOf(kGrid, original.indexOf("{#markdown}"));
  if (at < 0) throw new Error("the markdown slide's layout is not where this test expects it");
  const withTransition = (effect) => original.slice(0, at) + `${kGrid}\ntransition: ${effect}\nduration: 3` + original.slice(at + kGrid.length);

  // the second slide as it is once arrived, from a page opened on it
  let settledTruth = null;
  await withViewer(copy, async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html#view=markdown&step=0`);
    await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
    await page.waitFor("(globalThis.gr4Location || '').includes('view=markdown') && window.gr4Zoom ? 1 : false");
    for (let frame = 0; frame < 40; ++frame) {
      settledTruth = await shot(page);
    }
    await page.close();
  });

  for (const effect of kEffects) {
    writeFileSync(deck, withTransition(effect));
    await withViewer(copy, async ({ server, browser }) => {
      const page = await openPage(browser, `${server.origin}/index.html#view=intro&step=0`);
      await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
      await page.waitFor("(globalThis.gr4Location || '').includes('view=intro') && window.gr4Zoom ? 1 : false");
      await page.evaluate("document.getElementById('canvas').focus()");
      await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 1100, y: 600 });
      let before = null;
      for (let frame = 0; frame < 40; ++frame) {
        before = await shot(page);
      }
      const pressed = Date.now();
      await page.press(...kRight);
      const inFlight = [];
      while (Date.now() - pressed < 2700) {
        const image = await shot(page);
        if (Date.now() - pressed > 300) {
          inFlight.push(image);
        }
      }
      let after = null;
      for (let frame = 0; frame < 40; ++frame) {
        after = await shot(page);
      }
      const between = inFlight.filter((image) => differs(image, before) > 0.005 && differs(image, after) > 0.005);
      expect(between.length >= 2, `${effect}: in flight the screen is neither slide, ${between.length} of ${inFlight.length} frames`);
      expect(differs(after, settledTruth) < 0.01, `${effect}: and then exactly the slide it went to, ${(100 * differs(after, settledTruth)).toFixed(2)} % of pixels differ`);
      if (effect === "fire" || effect === "fire2") {
        const flame = Math.max(...inFlight.map(flameShare));
        expect(flame > 0.002, `${effect}: flame colours while it burns, ${(100 * flame).toFixed(2)} % of the screen at most`);
      }
      const broken = page.consoleLines.filter((line) => /shader|framebuffer/i.test(line));
      expect(broken.length === 0, `${effect}: the shader builds: ${broken.slice(0, 2).join(" | ") || "no complaint"}`);
      await page.close();
    });
  }
} finally {
  rmSync(copy, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserShaderTransitions: all checks passed" : `qa_BrowserShaderTransitions: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

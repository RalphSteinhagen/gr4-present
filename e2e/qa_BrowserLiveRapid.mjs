// Stepping quickly through the live slides must not leave the deck frozen
// Ground truth: a frozen runtime keeps its last frame on the canvas, so only a difference between frames shows life
// rapid presses outrun graph builds: one is built and another destroyed in the same frame, destroying joins a thread

import { openPage, withViewer } from "../devtools/browser_harness.mjs";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserLiveRapid.mjs <viewer-web directory>");
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

const viewOf = async (page) => (await page.evaluate("(globalThis.gr4Location || '')")).replace(/^#view=/, "").replace(/&.*$/, "");
const frameOf = async (page) => (await page.command("Page.captureScreenshot", { format: "png" })).result.data;

const walkTo = async (page, key, wanted, budget = 40) => {
  for (let step = 0; step < budget; ++step) {
    if ((await viewOf(page)) === wanted) {
      return true;
    }
    const before = await page.evaluate("(globalThis.gr4Location || '')");
    await page.press(...key);
    if (!(await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? (globalThis.gr4Location || '') : false`))) {
      return false;
    }
  }
  return false;
};

/// true when the canvas is still being repainted with something new
const stillRunning = async (page) => {
  // a graph that cannot be restarted is rebuilt after two seconds, so the deck gets longer than that to recover
  for (let settle = 0; settle < 90; ++settle) {
    await page.command("Page.captureScreenshot", { format: "png" });
  }
  const first = await frameOf(page);
  for (let attempt = 0; attempt < 20; ++attempt) {
    if ((await frameOf(page)) !== first) {
      return true;
    }
  }
  return false;
};

await withViewer(directory, async ({ server, browser }) => {
  const page = await openPage(browser, `${server.origin}/index.html#view=rejoin&step=0`);
  // a hang throws out of the walk, so the page's last words and whether its frames still change are printed
  const explain = async () => {
    // a screenshot comes from the compositor, so it answers even when the page's main thread does not
    const shot = () => Promise.race([frameOf(page), new Promise((resolve) => setTimeout(() => resolve("no screenshot"), 10000))]);
    const first = await shot();
    let moving = false;
    for (let attempt = 0; attempt < 10 && !moving; ++attempt) {
      moving = (await shot()) !== first;
    }
    console.error(`frames still changing: ${moving}\nconsole output:\n  ` + page.consoleLines.slice(-30).join("\n  "));
  };
  try {
    await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
    // the link's own fragment is there before the deck has loaded, so the deck's published state is waited for
    await page.waitFor("(globalThis.gr4Location || '').includes('view=') && (globalThis.gr4Location || '').includes('step=0') && window.gr4Zoom ? 1 : false");
    await page.evaluate("document.getElementById('canvas').focus()");
    await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 900, y: 690 });

    for (const [direction, key, from] of [
      ["forwards", kRight, "live"],
      ["backwards", kLeft, "live-graph"],
    ]) {
      expect(await walkTo(page, kRight, from), `reached ${from} to start from`);
      const before = await viewOf(page);

      for (let press = 0; press < 8; ++press) {
        await page.press(...key);
      }
      // presses are answered a frame at a time: stopped means still for longer than a graph takes to build
      let settled = await page.evaluate("(globalThis.gr4Location || '')");
      for (let since = Date.now(); Date.now() - since < 2500;) {
        const now = await page.evaluate("(globalThis.gr4Location || '')");
        if (now !== settled) {
          settled = now;
          since = Date.now();
        }
      }
      const after = await viewOf(page);
      expect(after !== before, `eight quick presses ${direction} move the deck, ${before} -> ${after}`);
      // graphs are checked on a live slide reached afterwards; a burst backwards can retrace to the deck's start
      await walkTo(page, direction === "forwards" ? kLeft : kRight, "live-timing", 120);
      expect(await stillRunning(page), `and live-timing, reached after eight quick presses ${direction}, is still drawing`);

      // a picture that stops changing means a stalled runtime or a dead chart: one more key press tells them apart
      const stamp = await page.evaluate("(globalThis.gr4Location || '')");
      await page.press(...key);
      const answered = await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(stamp)} ? (globalThis.gr4Location || '') : false`);
      expect(Boolean(answered), `and it still answers a key afterwards, ${direction} from ${after}`);
    }
  } catch (error) {
    failures.push(String(error));
    console.log(`  FAIL  ${error.message}`);
    await explain();
    await page.close();
    return;
  }
  if (failures.length > 0) {
    await explain();
  }
  // reported even without failure: a pool at its thread limit that queued instead of stopping the page is worth knowing
  const queued = page.consoleLines.filter((line) => line.includes("thread limit reached")).length;
  console.log(`thread-limit warnings: ${queued}`);
  await page.close();
});

console.log(failures.length === 0 ? "qa_BrowserLiveRapid: all checks passed" : `qa_BrowserLiveRapid: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

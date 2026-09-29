// Stepping back and forth across the live slides must not stall
// a live region owns a graph, a scheduler and a thread; destroying one joins its thread on the UI thread
// in the browser the pthread pool is fixed and nothing is proxied off the main thread, so that stalls the audience
// Ground truth: the deck's published cursor for where we are, and the canvas for whether anything is on it

import { openPage, withViewer } from "../devtools/browser_harness.mjs";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserLiveWalk.mjs <viewer-web directory>");
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

/// the views carrying `:::gr4` regions, in deck order
const kLive = ["live", "live-timing", "live-spectrum", "live-graph"];

const viewOf = async (page) => (await page.evaluate("(globalThis.gr4Location || '')")).replace(/^#view=/, "").replace(/&.*$/, "");

/// presses `key` until the cursor names `wanted`, and says how many frames it took to get there
const walkTo = async (page, key, wanted, budget = 40) => {
  for (let step = 0; step < budget; ++step) {
    if ((await viewOf(page)) === wanted) {
      return step;
    }
    const before = await page.evaluate("(globalThis.gr4Location || '')");
    await page.press(...key);
    // a stalled viewer stops publishing its cursor, which is what the deadline inside waitFor catches
    const moved = await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? (globalThis.gr4Location || '') : false`);
    if (!moved) {
      return -1;
    }
  }
  return -1;
};

/// the canvas as it stands; a stalled runtime keeps its last frame, so only a difference tells it from a running deck
const frameOf = async (page) => (await page.command("Page.captureScreenshot", { format: "png" })).result.data;

/// how much of the canvas is not the slide's near-black ground, as a fraction; a blank or frozen-black view is 0
const inkFraction = async (page) => {
  const shot = (await page.command("Page.captureScreenshot", { format: "png" })).result.data;
  return await page.evaluate(`(async () => {
        const image = new Image();
        image.src = "data:image/png;base64,${shot}";
        await image.decode();
        const canvas = document.createElement("canvas");
        canvas.width = image.width; canvas.height = image.height;
        const context = canvas.getContext("2d");
        context.drawImage(image, 0, 0);
        const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
        let lit = 0;
        for (let at = 0; at < pixels.length; at += 4) {
            if (pixels[at] + pixels[at + 1] + pixels[at + 2] > 120) { lit += 1; }
        }
        return lit / (pixels.length / 4);
    })()`);
};

const kStallWatchMs = 15000; // how long a frozen view is watched after the check fails, to tell slow from stopped

/// asserts the slide is painted and moving: a live chart advances every frame, a stalled runtime keeps its last frame
const assertLive = async (page, what) => {
  // each screenshot costs tens of milliseconds: a dozen frames for a graph to produce, a handful to show movement
  for (let frame = 0; frame < 12; ++frame) {
    await page.command("Page.captureScreenshot", { format: "png" });
  }
  const ink = await inkFraction(page);
  expect(ink > 0.02, `${what} is painted, ${(ink * 100).toFixed(1)}% of the canvas is lit`);

  const first = await frameOf(page);
  let moved = false;
  for (let attempt = 0; attempt < 12 && !moved; ++attempt) {
    moved = (await frameOf(page)) !== first;
  }
  if (!moved) {
    // the check stays as strict; the failure says whether the view was slow or stopped, and what the graphs reported
    const since = Date.now();
    let changedAfterMs = -1;
    while (changedAfterMs < 0 && Date.now() - since < kStallWatchMs) {
      changedAfterMs = (await frameOf(page)) !== first ? Date.now() - since : -1;
    }
    const graphs = await page.evaluate("JSON.stringify(window.gr4Graphs || {})");
    const outcome = changedAfterMs < 0 ? `still unchanged after ${kStallWatchMs / 1000} s` : `changed again after ${changedAfterMs} ms`;
    console.log(`  note  ${what}: ${outcome}; graphs ${graphs}`);
  }
  expect(moved, `${what} is still running, its picture changes between frames`);
};

await withViewer(directory, async ({ server, browser }) => {
  const page = await openPage(browser, `${server.origin}/index.html#view=rejoin&step=0`);
  await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
  // the link's own fragment is there before the deck has loaded, so the deck's published state is waited for
  expect(
    await page.waitFor("(globalThis.gr4Location || '').includes('view=') && (globalThis.gr4Location || '').includes('step=0') && window.gr4Zoom ? 1 : false"),
    "the viewer publishes its starting cursor",
  );
  await page.evaluate("document.getElementById('canvas').focus()");
  // parked away from the left edge, or the side menu opens over the slide and counts as ink
  await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 900, y: 690 });

  // two passes: the first visit builds a graph, the second reuses or rebuilds it; the stall is on the way back
  for (let pass = 1; pass <= 2; ++pass) {
    for (const view of kLive) {
      const began = Date.now();
      const reached = await walkTo(page, kRight, view);
      const took = Date.now() - began;
      expect(reached >= 0, `pass ${pass}: forwards to ${view}`);
      if (reached < 0) {
        break;
      }
      expect(took < 15000, `pass ${pass}: reaching ${view} took ${(took / 1000).toFixed(1)} s`);
      await assertLive(page, `pass ${pass}: ${view}`);
    }

    for (const view of [...kLive].reverse().slice(1)) {
      const began = Date.now();
      const reached = await walkTo(page, kLeft, view);
      const took = Date.now() - began;
      expect(reached >= 0, `pass ${pass}: backwards to ${view}`);
      if (reached < 0) {
        break;
      }
      expect(took < 15000, `pass ${pass}: stepping back to ${view} took ${(took / 1000).toFixed(1)} s`);
      await assertLive(page, `pass ${pass}: ${view} after stepping back`);
    }
  }

  // back-to-back keys queue a build and a teardown in consecutive frames, which the paced walk never creates
  for (const [direction, key] of [
    ["forwards", kRight],
    ["backwards", kLeft],
  ]) {
    await walkTo(page, kLeft, kLive[0]);
    const from = await viewOf(page);
    const began = Date.now();
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
    const took = Date.now() - began;
    expect(settled !== from, `pressing ${direction} eight times without waiting moves the deck, from ${from} to ${String(settled).replace(/^#view=/, "")}`);
    expect(took < 20000, `and it settles in ${(took / 1000).toFixed(1)} s`);
    // graphs are checked on a live slide reached afterwards; a burst backwards can retrace to the deck's start
    await walkTo(page, direction === "forwards" && (await viewOf(page)) !== "live" ? kLeft : kRight, "live-timing", 120);
    await assertLive(page, `live-timing, reached after eight quick presses ${direction},`);
  }

  const before = await page.evaluate("(globalThis.gr4Location || '')");
  await page.press(...kRight);
  expect(
    Boolean(await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? (globalThis.gr4Location || '') : false`)),
    "the deck still answers a key after walking the live slides twice",
  );

  if (failures.length > 0) {
    console.error("console output:\n  " + page.consoleLines.slice(-40).join("\n  "));
  }
  await page.close();
});

console.log(failures.length === 0 ? "qa_BrowserLiveWalk: all checks passed" : `qa_BrowserLiveWalk: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

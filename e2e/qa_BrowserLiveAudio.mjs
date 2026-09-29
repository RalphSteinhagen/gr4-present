// The live audio slide runs the microphone once granted and a recorded clap until then (`AudioSourceMultiplexed`, `standby:`)
// Chrome's fake media device supplies the microphone and its fake prompt grants it; without the flag it is refused
// Ground truth: the author's specification (2026-10-02): clap if refused or unanswered, microphone once granted

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt } from "../devtools/png.mjs";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserLiveAudio.mjs <viewer-web directory>");
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
const kFakeMicrophone = "--use-fake-device-for-media-stream";
const kGrantPrompt = "--use-fake-ui-for-media-stream";

/// the audio slide opened by its link, then a key: the gesture a browser wants before it plays anything
/// opened by link: the slide before is live too, and its charts would take the palette's first colours
const enterAudioSlide = async (server, browser) => {
  const page = await openPage(browser, `${server.origin}/index.html#view=live-audio&step=0`);
  await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
  await page.waitFor("(globalThis.gr4Location || '').includes('view=live-audio') && window.gr4Zoom ? 1 : false");
  await page.evaluate("document.getElementById('canvas').focus()");
  await page.press("n", "KeyN", 78);
  await page.press("n", "KeyN", 78);
  return page;
};

/// the slide before, then onto the audio one with a key; the microphone is asked for after the gesture, never before
const enterAudioSlideByKey = async (server, browser) => {
  const page = await openPage(browser, `${server.origin}/index.html#view=live-graph&step=0`);
  await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
  await page.waitFor("(globalThis.gr4Location || '').includes('view=live-graph') && window.gr4Zoom ? 1 : false");
  await page.evaluate("document.getElementById('canvas').focus()");
  // the slide before holds its neighbour's graphs ready but none that opens a device, microphone or standby recording
  for (let frame = 0; frame < 60; ++frame) {
    await page.command("Page.captureScreenshot", { format: "png" });
  }
  const held = Object.keys(JSON.parse(await page.evaluate("JSON.stringify(window.gr4Graphs || {})")));
  expect(
    held.length > 0 && !held.some((workflow) => workflow.includes("microphone") || workflow.includes("clap")),
    `the slide before holds no audio graph: ${JSON.stringify(held)}`,
  );
  await page.press(...kRight);
  await page.waitFor("(globalThis.gr4Location || '').includes('view=live-audio') ? 1 : false");
  return page;
};

/// which workflow the region draws once it has had time to settle on one
const sourceAfterSettling = async (page) => {
  let source = "";
  for (let frame = 0; frame < 120; ++frame) {
    await page.command("Page.captureScreenshot", { format: "png" });
    source = await page.evaluate("(window.gr4LiveSource || {}).audio || ''");
  }
  return source;
};

/// chart changes per two-second window of eight once settled: running graphs every frame, loop-fed ones once per loop
const changesPerWindow = async (page) => {
  const clip = { x: 140, y: 110, width: 1080, height: 380, scale: 1 }; // the dashboard: time chart, triggered chart, spectrum and waterfall
  const windows = [0, 0, 0, 0];
  let previous = "";
  const started = Date.now();
  while (Date.now() - started < 8000) {
    const shot = (await page.command("Page.captureScreenshot", { format: "png", clip })).result.data;
    windows[Math.min(3, Math.floor((Date.now() - started) / 2000))] += shot !== previous ? 1 : 0;
    previous = shot;
  }
  return windows;
};

/// topmost row the left channel reaches per frame over eight seconds
/// the chart spans -1..1 FS at rows 124..306 at 1280x720; a clap of 0.8 FS reaches its top eighth, noise never does
const leftChannelTops = async (page) => {
  const tops = [];
  const started = Date.now();
  while (Date.now() - started < 8000) {
    const image = decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
    let top = -1;
    for (let y = 124; y < 307 && top < 0; ++y) {
      for (let x = 150; x < 626; x += 2) {
        const pixel = pixelAt(image, x, y);
        // the left channel takes the deck's first series colour, #FDB342 (Theme.hpp)
        if (pixel.r > 200 && pixel.g > 130 && pixel.g < 215 && pixel.b < 120) {
          top = y;
          break;
        }
      }
    }
    if (top >= 0) {
      tops.push(top);
    }
  }
  return tops.sort((a, b) => a - b);
};

await withViewer(
  directory,
  async ({ server, browser }) => {
    const page = await enterAudioSlide(server, browser);
    expect((await sourceAfterSettling(page)) === "standby", "refused the microphone, the slide runs the recorded clap");
    const windows = await changesPerWindow(page);
    expect(
      windows.every((changes) => changes >= 5),
      `and its chart keeps moving, changes per two seconds: ${windows.join(", ")}`,
    );
    const tops = await leftChannelTops(page);
    expect(tops.length > 10 && tops[0] <= 147, `the clap stands out of the noise: the left channel reaches row ${tops[0]}, the top eighth ends at 147`);
    expect(
      page.consoleLines.some((line) => line.includes("Microphone permission denied")),
      "the refusal came from the browser, not from a missing device",
    );

    await page.command("Browser.grantPermissions", { origin: server.origin, permissions: ["audioCapture"] });
    await page.press(...kRight);
    await page.waitFor("!(globalThis.gr4Location || '').includes('view=live-audio') ? 1 : false"); // away, whichever slide follows
    await page.press(...kLeft);
    await page.waitFor("(globalThis.gr4Location || '').includes('view=live-audio') ? 1 : false");
    expect((await sourceAfterSettling(page)) === "workflow", "granted later, the next visit runs the microphone");
    await page.close();
  },
  { browserArguments: [kFakeMicrophone] },
);

await withViewer(
  directory,
  async ({ server, browser }) => {
    const page = await enterAudioSlideByKey(server, browser);
    expect((await sourceAfterSettling(page)) === "workflow", "granted at once, the slide runs the microphone");
    const windows = await changesPerWindow(page);
    expect(
      windows.every((changes) => changes >= 5),
      `and its chart keeps moving, changes per two seconds: ${windows.join(", ")}`,
    );
    await page.close();
  },
  { browserArguments: [kFakeMicrophone, kGrantPrompt] },
);

console.log(failures.length === 0 ? "qa_BrowserLiveAudio: all checks passed" : `qa_BrowserLiveAudio: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

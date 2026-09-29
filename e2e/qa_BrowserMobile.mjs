// The deck on a phone, upright and turned: the slide fits the screen, and quick swipes forwards and backwards move the
// deck without leaving it frozen.
//
// Chrome emulates the phones the author named -- Samsung Galaxy S20 Ultra and iPhone 14 Pro Max -- by their CSS size,
// pixel density and touch input; their own browsers' engines are not covered. A swipe is a touch dragged across most
// of the screen, as a finger turns a page: leftwards for the next view. What is asserted is what a presenter would see:
// every word of the slide on screen, the view the swipes moved to, a deck that still answers a swipe afterwards, and a
// live slide whose picture still changes from frame to frame (a frozen runtime keeps its last frame on the canvas).

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng } from "../devtools/png.mjs";
import { createHash } from "node:crypto";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserMobile.mjs <viewer-web directory>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const kPhones = [
  { name: "Galaxy S20 Ultra", width: 412, height: 915, density: 3.5 },
  { name: "iPhone 14 Pro Max", width: 430, height: 932, density: 3 },
];

const settle = async (page, frames) => {
  for (let frame = 0; frame < frames; ++frame) {
    await page.command("Page.captureScreenshot", { format: "png" });
  }
};

const swipe = async (page, width, height, towardsNext) => {
  const y = Math.round(height * 0.5);
  const from = Math.round(width * (towardsNext ? 0.8 : 0.2));
  const to = Math.round(width * (towardsNext ? 0.2 : 0.8));
  await page.command("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: [{ x: from, y }] });
  for (let step = 1; step <= 6; ++step) {
    await page.command("Input.dispatchTouchEvent", { type: "touchMove", touchPoints: [{ x: from + ((to - from) * step) / 6, y }] });
  }
  await page.command("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] });
};

// how many different pictures a region showed over two seconds, sampled in the screenshot's own pixels
const picturesOver2s = async (page, [x, y, width, height], cssWidth) => {
  const seen = new Set();
  const until = Date.now() + 2000;
  while (Date.now() < until) {
    const image = decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
    const scale = image.width / cssWidth;
    const hash = createHash("md5");
    for (let row = Math.floor(y * scale); row < (y + height) * scale; row += 3) {
      const start = (row * image.width + Math.floor(x * scale)) * 4;
      hash.update(image.rgba.subarray(start, start + Math.floor(width * scale) * 4));
    }
    seen.add(hash.digest("hex"));
  }
  return seen.size;
};

const viewOf = async (page) => (await page.evaluate("(globalThis.gr4Location || '')")).replace(/^#view=([^&]*).*$/, "$1");

await withViewer(directory, async ({ server, browser }) => {
  for (const phone of kPhones) {
    for (const upright of [true, false]) {
      const width = upright ? phone.width : phone.height;
      const height = upright ? phone.height : phone.width;
      const label = `${phone.name} ${upright ? "upright" : "turned"} (${width}x${height})`;
      const openPhone = async (view) => {
        const opened = await openPage(browser, `${server.origin}/index.html#view=${view}&step=0`);
        await opened.command("Emulation.setDeviceMetricsOverride", {
          width,
          height,
          deviceScaleFactor: phone.density,
          mobile: true,
          screenOrientation: { type: upright ? "portraitPrimary" : "landscapePrimary", angle: upright ? 0 : 90 },
        });
        await opened.command("Emulation.setTouchEmulationEnabled", { enabled: true, maxTouchPoints: 5 });
        await opened.waitFor(`document.getElementById('canvas') && window.gr4Zoom && (globalThis.gr4Location || '').includes('view=${view}&') ? 1 : false`);
        return opened;
      };
      let page = await openPhone("live");
      await settle(page, 60);

      // the slide fits: every word it set lies on the screen
      const runs = JSON.parse(await page.evaluate("JSON.stringify(window.gr4TextRuns || [])"));
      const outside = runs.filter(([x, y, w, h]) => x < -1 || y < -1 || x + w > width + 1 || y + h > height + 1);
      expect(runs.length > 0 && outside.length === 0, `${label}: the slide's ${runs.length} words are all on screen${outside.length ? `, not ${JSON.stringify(outside.slice(0, 3))}` : ""}`);

      // quick swipes forwards, without waiting for the deck: it moves, and still answers afterwards. From the pictures,
      // opened afresh, which have slides enough after them; the live slide sits near the end of the deck.
      await page.close();
      page = await openPhone("pictures");
      await settle(page, 20);
      const start = await viewOf(page);
      for (let flick = 0; flick < 8; ++flick) {
        await swipe(page, width, height, true);
      }
      await settle(page, 40);
      const ahead = await viewOf(page);
      expect(ahead !== start, `${label}: eight quick swipes forwards move the deck, ${start} -> ${ahead}`);
      // by where it is, the step included: a slide with steps answers by taking the next one
      const reached = await page.evaluate("(globalThis.gr4Location || '')");
      await swipe(page, width, height, true);
      await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(reached)} ? 1 : false`).catch(() => {});
      const answered = await page.evaluate("(globalThis.gr4Location || '')");
      expect(answered !== reached, `${label}: and it still answers a swipe afterwards, ${reached} -> ${answered}`);

      // and back again
      const before = await viewOf(page);
      for (let flick = 0; flick < 8; ++flick) {
        await swipe(page, width, height, false);
      }
      await settle(page, 40);
      expect((await viewOf(page)) !== before, `${label}: eight quick swipes backwards move it back, ${before} -> ${await viewOf(page)}`);

      // a live slide reached after all that is still drawing, by keys: the viewer reads its address only as it loads
      for (let press = 0; press < 120 && (await viewOf(page)) !== "live"; ++press) {
        const before = await page.evaluate("(globalThis.gr4Location || '')");
        await page.press("ArrowRight", "ArrowRight", 39);
        await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? 1 : false`);
      }
      await settle(page, 60);
      const region = Object.values(JSON.parse(await page.evaluate("JSON.stringify(window.gr4Regions || {})")))[0];
      expect(region !== undefined && (await picturesOver2s(page, region, width)) > 1, `${label}: the live chart is still drawing`);
      await page.close();
    }
  }
});

console.log(failures.length === 0 ? "qa_BrowserMobile: all checks passed" : `qa_BrowserMobile: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

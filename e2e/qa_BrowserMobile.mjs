// The deck on a phone, upright and turned: the slide fits the screen and quick swipes move the deck without freezing it.
// Chrome emulation covers CSS size, pixel density and touch input of the named phones, not their browser engines.

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

      const runs = JSON.parse(await page.evaluate("JSON.stringify(window.gr4TextRuns || [])"));
      const outside = runs.filter(([x, y, w, h]) => x < -1 || y < -1 || x + w > width + 1 || y + h > height + 1);
      expect(
        runs.length > 0 && outside.length === 0,
        `${label}: the slide's ${runs.length} words are all on screen${outside.length ? `, not ${JSON.stringify(outside.slice(0, 3))}` : ""}`,
      );

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
      const reached = await page.evaluate("(globalThis.gr4Location || '')");
      await swipe(page, width, height, true);
      await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(reached)} ? 1 : false`).catch(() => {});
      const answered = await page.evaluate("(globalThis.gr4Location || '')");
      expect(answered !== reached, `${label}: and it still answers a swipe afterwards, ${reached} -> ${answered}`);

      const before = await viewOf(page);
      for (let flick = 0; flick < 8; ++flick) {
        await swipe(page, width, height, false);
      }
      await settle(page, 40);
      expect((await viewOf(page)) !== before, `${label}: eight quick swipes backwards move it back, ${before} -> ${await viewOf(page)}`);

      // by keys, because the viewer reads its address only as it loads
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

// Ground truth: Chrome refuses a fullscreen request made without transient user activation, and a granted request uses that activation up
await withViewer(directory, async ({ server, browser }) => {
  const page = await openPage(browser, `${server.origin}/index.html#view=pictures&step=0`);
  await page.command("Emulation.setTouchEmulationEnabled", { enabled: true, maxTouchPoints: 5 });
  await page.waitFor("document.getElementById('canvas') && window.gr4Zoom && (globalThis.gr4Location || '').includes('view=') ? 1 : false");
  await page.evaluate(`(() => {
    globalThis.fullscreenRequests = [];
    const request = Element.prototype.requestFullscreen;
    Element.prototype.requestFullscreen = function (...options) {
      const answer = { outcome: "pending" };
      globalThis.fullscreenRequests.push(answer);
      const promise = request.apply(this, options);
      promise.then(() => { answer.outcome = "granted"; }, (error) => { answer.outcome = error.message; });
      return promise;
    };
  })()`);
  const tap = async () => {
    const at = { x: 200, y: 300 };
    await page.command("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: [at] });
    await settle(page, 6);
    await page.command("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] });
    await page.command("Input.dispatchMouseEvent", { type: "mousePressed", ...at, button: "left", clickCount: 1 });
    await page.command("Input.dispatchMouseEvent", { type: "mouseReleased", ...at, button: "left", clickCount: 1 });
  };
  await tap();
  for (let gesture = 0; gesture < 4; ++gesture) {
    await settle(page, 20);
    await (gesture % 2 === 0 ? page.press("ArrowRight", "ArrowRight", 39) : tap());
  }
  await settle(page, 20);
  const requests = JSON.parse(await page.evaluate("JSON.stringify(globalThis.fullscreenRequests)"));
  const refused = requests.filter((request) => request.outcome !== "granted");
  expect(requests.length >= 1 && refused.length === 0, `a tap enters fullscreen and no later tap or key asks again unanswered, ${JSON.stringify(requests)}`);
  await page.close();
});

console.log(failures.length === 0 ? "qa_BrowserMobile: all checks passed" : `qa_BrowserMobile: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

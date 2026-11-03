// "dark colours" in the side menu forces the other colour scheme, whatever the browser prefers, and back again.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt } from "../devtools/png.mjs";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserScheme.mjs <viewer-web directory>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const nextFrame = () => new Promise((resolve) => setTimeout(resolve, 100));
const click = async (page, point) => {
  await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: point.x, y: point.y });
  await nextFrame();
  for (const type of ["mousePressed", "mouseReleased"]) {
    await page.command("Input.dispatchMouseEvent", { type, x: point.x, y: point.y, button: "left", clickCount: 1 });
    await nextFrame();
  }
};
const utilityEntry = async (page, label) => {
  const probe = `(() => { const entry = (window.gr4FileEntries || []).find((e) => e[0] === "utility" && e[6].endsWith(${JSON.stringify(label)})); return entry && entry[2] >= 0 ? JSON.stringify(entry) : ""; })()`;
  for (let attempt = 0; attempt < 100; ++attempt) {
    await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 4 + (attempt % 2), y: 300 });
    const found = await page.evaluate(probe);
    if (found) {
      const entry = JSON.parse(found);
      return { x: entry[2] + entry[4] / 2, y: entry[3] + entry[5] / 2 };
    }
    await nextFrame();
  }
  throw new Error(`the side menu never showed ${label}`);
};
// the slide's background, read off the right edge where nothing is drawn
const backgroundOf = async (page) => {
  const shot = decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
  const pixel = pixelAt(shot, shot.width - 2, Math.floor(shot.height / 2));
  return (pixel.r + pixel.g + pixel.b) / 3;
};
const untilBackground = async (page, isWanted) => {
  let brightness = -1;
  for (let attempt = 0; attempt < 50 && !isWanted(brightness); ++attempt) {
    brightness = await backgroundOf(page);
    await nextFrame();
  }
  return brightness;
};

await withViewer(directory, async ({ server, browser }) => {
  const page = await openPage(browser, `${server.origin}/index.html?theme=light`);
  expect(await page.waitFor("(globalThis.gr4Location || '').includes('view=')", 120_000), "the deck opens");
  await page.evaluate("document.getElementById('canvas').focus()");
  const light = await untilBackground(page, (brightness) => brightness > 230);
  expect(light > 230, `?theme=light starts with a light background (${light.toFixed(0)})`);

  await click(page, await utilityEntry(page, "dark colours"));
  const dark = await untilBackground(page, (brightness) => brightness >= 0 && brightness < 30);
  expect(dark >= 0 && dark < 30, `"dark colours" switches to the dark scheme (${dark.toFixed(0)})`);
  // the box is drawn ticked: the label begins with the checked glyph, the one the "follow edits" box uses
  const label = await page.evaluate(`JSON.stringify(((window.gr4FileEntries || []).find((e) => e[0] === "utility" && e[6].endsWith("dark colours")) || [])[6] || "")`);
  expect(label.includes("\uf14a") || label.includes("\uf14a"), `and the entry shows a ticked box: ${label}`);

  await click(page, await utilityEntry(page, "dark colours"));
  const again = await untilBackground(page, (brightness) => brightness > 230);
  expect(again > 230, `a second click returns to the light scheme (${again.toFixed(0)})`);
  if (failures.length > 0) {
    console.error("console output:\n  " + page.consoleLines.slice(-30).join("\n  "));
  }
  await page.close();

  // a browser that prefers dark: the page follows it, and the box forces light over that preference
  const preferring = await openPage(browser, "about:blank");
  await preferring.command("Emulation.setEmulatedMedia", { features: [{ name: "prefers-color-scheme", value: "dark" }] });
  await preferring.command("Page.navigate", { url: `${server.origin}/index.html` });
  expect(await preferring.waitFor("(globalThis.gr4Location || '').includes('view=')", 120_000), "the deck opens for a browser preferring dark");
  await preferring.evaluate("document.getElementById('canvas').focus()");
  const followed = await untilBackground(preferring, (brightness) => brightness >= 0 && brightness < 30);
  expect(followed >= 0 && followed < 30, `without a forced scheme the page follows the preference (${followed.toFixed(0)})`);
  await click(preferring, await utilityEntry(preferring, "dark colours"));
  const forced = await untilBackground(preferring, (brightness) => brightness > 230);
  expect(forced > 230, `unticking the box forces light over the browser's preference (${forced.toFixed(0)})`);
  await preferring.close();

  const forcedDark = await openPage(browser, `${server.origin}/index.html?theme=dark`);
  expect(await forcedDark.waitFor("(globalThis.gr4Location || '').includes('view=')", 120_000), "?theme=dark opens");
  await forcedDark.evaluate("document.getElementById('canvas').focus()");
  const ticked = await (async () => {
    for (let attempt = 0; attempt < 100; ++attempt) {
      await forcedDark.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 4 + (attempt % 2), y: 300 });
      const found = await forcedDark.evaluate(`JSON.stringify(((window.gr4FileEntries || []).find((e) => e[0] === "utility" && e[6].endsWith("dark colours")) || [])[6] || "")`);
      if (found.length > 2) {
        return found;
      }
      await nextFrame();
    }
    return "";
  })();
  expect(ticked.includes("\uf14a"), `?theme=dark starts with the box ticked: ${ticked}`);
  await forcedDark.close();
});

process.exit(failures.length === 0 ? 0 : 1);

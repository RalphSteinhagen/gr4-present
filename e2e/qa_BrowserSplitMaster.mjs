// A slide master drawn in three groups, `top`, `middle` and `bottom`, is composed to the screen: the top and bottom
// bands keep their shape at the screen's width and are pinned to its top and foot, and the middle stretches to the
// height between them. One drawing then serves a landscape and a portrait screen alike.
//
// Ground truth is the drawing written here: a 1600 x 900 master whose top band (140 high) is red, middle (660) green
// and bottom (100) blue. At a screen width W the red band must be 140 W / 1600 rows tall from the top, the blue band
// 100 W / 1600 rows tall at the foot, and green fills the rest; the heading is drawn inside the red band.
//
// A second section names a portrait master as well, 900 x 1600 with bands of 300, 1150 and 150: a screen taller than
// wide takes that one, so its red band is 300 W / 900 rows and its blue 150 W / 900, and a landscape screen still
// takes the first.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt } from "../devtools/png.mjs";
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserSplitMaster.mjs <viewer-web directory>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const kMaster = `<svg xmlns="http://www.w3.org/2000/svg" width="1600" height="900" viewBox="0 0 1600 900">
  <g id="top"><rect x="0" y="0" width="1600" height="140" fill="#c00000"/>
    <rect id="title" data-present="markdown" x="60" y="25" width="1480" height="90" fill="none"/></g>
  <g id="middle"><rect x="0" y="140" width="1600" height="660" fill="#006000"/>
    <rect id="content" data-present="markdown" x="60" y="180" width="1480" height="580" fill="none"/></g>
  <g id="bottom"><rect x="0" y="800" width="1600" height="100" fill="#0000c0"/>
    <rect id="footer" data-present="markdown" x="60" y="825" width="1480" height="50" fill="none"/></g>
</svg>
`;

const kPortraitMaster = `<svg xmlns="http://www.w3.org/2000/svg" width="900" height="1600" viewBox="0 0 900 1600">
  <g id="top"><rect x="0" y="0" width="900" height="300" fill="#c00000"/>
    <rect id="title" data-present="markdown" x="40" y="40" width="820" height="220" fill="none"/></g>
  <g id="middle"><rect x="0" y="300" width="900" height="1150" fill="#006000"/>
    <rect id="content" data-present="markdown" x="40" y="340" width="820" height="1070" fill="none"/></g>
  <g id="bottom"><rect x="0" y="1450" width="900" height="150" fill="#0000c0"/>
    <rect id="footer" data-present="markdown" x="40" y="1480" width="820" height="90" fill="none"/></g>
</svg>
`;

const colourOf = (pixel) => (pixel.r > 150 && pixel.g < 60 && pixel.b < 60 ? "red" : pixel.g > 60 && pixel.r < 60 && pixel.b < 60 ? "green" : pixel.b > 150 && pixel.r < 60 && pixel.g < 60 ? "blue" : "other");

const copy = mkdtempSync(join(tmpdir(), "split-master-"));
try {
  cpSync(directory, copy, { recursive: true });
  writeFileSync(join(copy, "default", "figures", "split.svg"), kMaster);
  writeFileSync(join(copy, "default", "figures", "split-portrait.svg"), kPortraitMaster);
  const deck = join(copy, "default", "talk.md");
  const text = readFileSync(deck, "utf8");
  const second = text.indexOf("\n# ", text.indexOf("{#intro}"));
  writeFileSync(deck, text.slice(0, second + 1) + "# A split master keeps its bands in landscape and in portrait {#split}\n\n:::layout\nsource: figures/split.svg\n:::\n\nThe middle band stretches; the top and bottom keep their shape.\n\n" + "# A portrait screen takes the portrait master {#split-portrait}\n\n:::layout\nsource: figures/split.svg\nportrait: figures/split-portrait.svg\n:::\n\nThe sidebar would sit under the prose here.\n\n" + text.slice(second + 1));

  const kLandscapeBands = { width: 1600, top: 140, bottom: 100 };
  const kPortraitBands = { width: 900, top: 300, bottom: 150 };
  for (const [view, width, height, bands] of [["split", 1280, 720, kLandscapeBands], ["split", 720, 1280, kLandscapeBands], ["split-portrait", 1280, 720, kLandscapeBands], ["split-portrait", 720, 1280, kPortraitBands], ["split-portrait", 390, 844, kPortraitBands]]) {
    await withViewer(copy, async ({ server, browser }) => {
      const page = await openPage(browser, "about:blank");
      await page.command("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor: 1, mobile: false });
      await page.command("Page.navigate", { url: `${server.origin}/index.html#view=${view}&step=0` });
      await page.waitFor(`document.getElementById('canvas') && window.gr4Zoom && (globalThis.gr4Location || '').includes('view=${view}&') ? 1 : false`);
      let shot = "";
      for (let frame = 0; frame < 60; ++frame) {
        shot = (await page.command("Page.captureScreenshot", { format: "png" })).result.data;
      }
      const image = decodePng(Buffer.from(shot, "base64"));
      // down the right edge, clear of the side menu and of the text
      const column = image.width - 4;
      const colours = Array.from({ length: image.height }, (_, y) => colourOf(pixelAt(image, column, y)));
      const redEnd = colours.lastIndexOf("red") + 1;
      const blueStart = colours.indexOf("blue");
      const factor = width / bands.width;
      const label = `${view} ${width}x${height}`;
      expect(Math.abs(redEnd - bands.top * factor) <= 2, `${label}: the top band of the ${bands.width}-wide master keeps its shape, ${redEnd} rows against ${(bands.top * factor).toFixed(1)}`);
      expect(Math.abs(image.height - blueStart - bands.bottom * factor) <= 2, `${label}: its bottom band keeps its shape at the foot, ${image.height - blueStart} rows against ${(bands.bottom * factor).toFixed(1)}`);
      const greens = colours.slice(redEnd + 2, blueStart - 2);
      expect(greens.length > 0 && greens.every((colour) => colour === "green"), `${label}: the middle stretches over the ${blueStart - redEnd} rows between`);
      // the heading is drawn in the top band: light ink on red, somewhere in its rows
      let inked = 0;
      for (let y = 0; y < redEnd; ++y) {
        for (let x = Math.floor(width * 0.3); x < width - 10; x += 2) {
          const pixel = pixelAt(image, x, y);
          inked += pixel.g > 120 && pixel.b > 120 ? 1 : 0;
        }
      }
      expect(inked > 20, `${label}: the heading sits in the top band, ${inked} light pixels there`);
      // the footer line and the slide number share the blue band: the line must stop short of the number, however
      // narrow the screen, with at least a few pixels of plain band between them
      const isInk = (pixel) => pixel.r > 50 && pixel.g > 50;
      const inkColumns = Array.from({ length: image.width }, (_, x) => {
        for (let y = blueStart + 2; y < image.height - 2; ++y) {
          if (isInk(pixelAt(image, x, y))) {
            return true;
          }
        }
        return false;
      });
      // the rightmost run of ink, with gaps narrower than a word space bridged, is the number alone when the line
      // stops short of it; a line that runs into the number makes that run far wider than two digits can be
      const kBridged = 8; // px: wider than a word space or the gap inside a number at these sizes
      const right = inkColumns.lastIndexOf(true);
      let left = right;
      for (let blank = 0, x = right; x >= 0 && blank < kBridged; --x) {
        blank = inkColumns[x] ? 0 : blank + 1;
        if (inkColumns[x]) {
          left = x;
        }
      }
      const statusPixels = height * 0.022; // the status size the footer is set in
      expect(right - left + 1 <= 2.5 * statusPixels && inkColumns.slice(0, left).includes(true), `${label}: the footer line stops short of the slide number, which stands alone over ${right - left + 1} px at the right (at most ${(2.5 * statusPixels).toFixed(0)})`);
      await page.close();
    });
  }
} finally {
  rmSync(copy, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserSplitMaster: all checks passed" : `qa_BrowserSplitMaster: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

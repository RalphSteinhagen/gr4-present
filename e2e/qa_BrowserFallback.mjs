// A live region keeps a recording, `fallback:` in its block, and the viewer shows it, marked as recorded, when the
// region cannot run: here a workflow that does not exist. The presenter's window, which runs no graphs, shows the
// recording where the audience sees the live chart.
//
// Ground truth is the recording itself: an opaque PNG decoded here, whose mean colour the region must show once the
// viewer has fitted it into the box. A missing workflow is also named in the problems list.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt } from "../devtools/png.mjs";
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync, mkdirSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { deflateSync } from "node:zlib";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserFallback.mjs <viewer-web directory>");
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
// One flat magenta, a colour nothing on the deck uses, written as a PNG here: whatever the region shows can then only
// be the recording, and the colour it must show is known exactly.
const kMagenta = { r: 255, g: 0, b: 255 };
const solidPng = (width, height, { r, g, b }) => {
  const crcTable = Array.from({ length: 256 }, (_, n) => {
    let c = n;
    for (let k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    return c >>> 0;
  });
  const crc = (bytes) => {
    let c = 0xffffffff;
    for (const byte of bytes) c = crcTable[(c ^ byte) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  };
  const chunk = (type, data) => {
    const body = Buffer.concat([Buffer.from(type, "ascii"), data]);
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length);
    const check = Buffer.alloc(4);
    check.writeUInt32BE(crc(body));
    return Buffer.concat([length, body, check]);
  };
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8; // bits per channel
  header[9] = 2; // RGB
  const rows = Buffer.alloc((width * 3 + 1) * height);
  for (let y = 0; y < height; ++y) {
    for (let x = 0; x < width; ++x) {
      rows.set([r, g, b], y * (width * 3 + 1) + 1 + x * 3);
    }
  }
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk("IHDR", header), chunk("IDAT", deflateSync(rows)), chunk("IEND", Buffer.alloc(0))]);
};
const recording = { width: 160, height: 90 };

const meanOf = (image, x0, y0, width, height) => {
  let r = 0;
  let g = 0;
  let b = 0;
  let count = 0;
  for (let y = Math.floor(y0); y < Math.floor(y0 + height); y += 2) {
    for (let x = Math.floor(x0); x < Math.floor(x0 + width); x += 2) {
      const pixel = pixelAt(image, x, y);
      r += pixel.r;
      g += pixel.g;
      b += pixel.b;
      ++count;
    }
  }
  return { r: r / count, g: g / count, b: b / count };
};
const apart = (left, right) => Math.max(Math.abs(left.r - right.r), Math.abs(left.g - right.g), Math.abs(left.b - right.b));

const copy = mkdtempSync(join(tmpdir(), "fallback-"));
try {
  cpSync(directory, copy, { recursive: true });
  mkdirSync(join(copy, "default", "fallback"), { recursive: true });
  writeFileSync(join(copy, "default", "fallback", "recorded.png"), solidPng(recording.width, recording.height, kMagenta));
  const deck = join(copy, "default", "talk.md");
  let text = readFileSync(deck, "utf8");
  // the first live slide loses its workflow and gains a recording; the second keeps its workflow and gains one
  const live = text.indexOf(":::gr4", text.indexOf("{#live}"));
  text = text.slice(0, live) + text.slice(live).replace("workflow: workflows/controlled-sines.grc", "workflow: workflows/not-there.grc\nfallback: fallback/recorded.png");
  const timing = text.indexOf(":::gr4", text.indexOf("{#live-timing}"));
  text = text.slice(0, timing) + text.slice(timing).replace(/workflow: (\S+)/, "workflow: $1\nfallback: fallback/recorded.png");
  writeFileSync(deck, text);

  const visit = async (page, view) => {
    for (let press = 0; press < 200 && !(await page.evaluate("(globalThis.gr4Location || '')")).includes(`view=${view}&`); ++press) {
      const before = await page.evaluate("(globalThis.gr4Location || '')");
      await page.press(...kRight);
      await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? 1 : false`);
    }
    let shot = "";
    for (let frame = 0; frame < 90; ++frame) {
      shot = (await page.command("Page.captureScreenshot", { format: "png" })).result.data;
    }
    const regions = JSON.parse(await page.evaluate("JSON.stringify(window.gr4Regions || {})"));
    return { image: decodePng(Buffer.from(shot, "base64")), region: Object.values(regions)[0] };
  };
  // where the recording lands: fitted inside the region's box, keeping its shape, centred
  const fittedMean = ({ image, region }, below = Infinity) => {
    const [x, y, width, height] = region;
    const factor = Math.min(width / recording.width, height / recording.height);
    const w = recording.width * factor;
    const h = recording.height * factor;
    // the inner part, clear of the edges where scaling blends with the slide, of the label in the corner, and of
    // anything drawn over the slide from `below` down
    const top = y + (height - h) / 2 + h * 0.1;
    return meanOf(image, x + (width - w) / 2 + w * 0.1, top, w * 0.8, Math.min(h * 0.7, below - top));
  };
  const truth = kMagenta;

  for (const presenter of [false, true]) {
    await withViewer(copy, async ({ server, browser }) => {
      const page = await openPage(browser, `${server.origin}/index.html${presenter ? "?presenter" : ""}`);
      await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
      await page.waitFor("document.getElementById('canvas') && (globalThis.gr4Location || '').includes('view=') ? 1 : false");
      await page.evaluate("document.getElementById('canvas').focus()");
      await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 1100, y: 600 });

      if (!presenter) {
        const broken = await visit(page, "live");
        expect(broken.region !== undefined && apart(fittedMean(broken), truth) < 12, `a region whose workflow is missing shows its recording, mean colour ${JSON.stringify(broken.region && fittedMean(broken))} against ${JSON.stringify(truth)}`);
        expect(page.consoleLines.some((line) => line.includes("not-there.grc")), "and the missing workflow is named in the problems list");
      }
      const working = await visit(page, "live-timing");
      // the presenter's notes panel covers the lower third of its slide
      const distance = working.region === undefined ? 0 : apart(fittedMean(working, presenter ? 480 : Infinity), truth);
      if (presenter) {
        expect(working.region !== undefined && distance < 12, `the presenter's window shows the recording of a working region, ${distance.toFixed(1)} levels from it`);
      } else {
        expect(working.region !== undefined && distance > 20, `while the audience's window shows the live chart there, ${distance.toFixed(1)} levels from the recording`);
      }
      await page.close();
    });
  }
} finally {
  rmSync(copy, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserFallback: all checks passed" : `qa_BrowserFallback: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

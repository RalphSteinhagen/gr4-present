// A live region draws its workflow's dashboard as the workflow's `dashboard:` section lays it out, on the slide's
// own background: no window, frame or plot background of OpenDigitizer's in between.
//
// Ground truth is the workflow written here: two charts in a `Row`, the left one plotting only a green signal and the
// right one only a purple one, so the layout says where each colour may appear -- each in its own half of the
// region. The slide's background is read from the slide itself, outside the region, in light and in dark.
//
// A region asking for `status: here` gets OpenDigitizer's status bar in a strip at its foot, showing the latest
// warning the process logged. The warning is the region's own: a `legend:` naming no position, which the viewer
// reports through GR4's log; OpenDigitizer draws a warning in amber, rgb(255, 166, 0).
//
// The charts are set as the deck's own text: their axis labels take the colour of the slide's body text, read from
// that text on the same screenshot, and their tick digits are no smaller than the deck's 12 pt floor -- 1 pt being
// 1.333 px at 1280 x 720, and a digit of the body face, Liberation Sans, standing 1409 units tall in a pixel size that
// spans its 1854 units of ascent and 434 of descent.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt, pixelMatches } from "../devtools/png.mjs";
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserDashboard.mjs <viewer-web directory>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const kGreen = { r: 0x00, g: 0xc8, b: 0x00 };
const kPurple = { r: 0xc8, g: 0x00, b: 0xc8 };
const sink = (name, colour, signal) => `  - id: opendigitizer::ImPlotSink<float32>
    parameters:
      name: ${name}
      color: ${colour}
      signal_name: ${signal}
      signal_quantity: voltage
      signal_unit: V`;
const source = (name, frequency) => `  - id: gr::basic::SignalGenerator<float32>
    parameters:
      name: ${name}
      signal_type: Sin
      frequency: ${frequency}
      sample_rate: 1000
      chunk_size: 40`;
const plot = (name, sinkName) => `    - name: ${name}
      axes:
        - axis: X
          min: NaN
          max: NaN
        - axis: Y
          min: NaN
          max: NaN
      sources:
        - ${sinkName}`;
const twoCharts = `blocks:
${source("leftSource", 0.5)}
${source("rightSource", 0.7)}
${sink("leftSink", "0x00C800", "left")}
${sink("rightSink", "0xC800C8", "right")}
connections:
  - [ leftSource, 0, leftSink, 0 ]
  - [ rightSource, 0, rightSink, 0 ]
dashboard:
  layout: Row
  windowLayout: {} # the layout type places the charts; OpenDigitizer asks for this or a rect per plot
  sources:
    - name: leftSink
      block: leftSink
    - name: rightSink
      block: rightSink
  plots:
${plot("Left", "leftSink")}
${plot("Right", "rightSink")}
`;

// the columns and the share of pixels in the region that are of one colour, or of the slide's background
const survey = (image, [x0, y0, width, height], background) => {
  const columns = (colour) => {
    let first = Infinity;
    let last = -Infinity;
    for (let y = Math.floor(y0); y < y0 + height; y += 2) {
      for (let x = Math.floor(x0); x < x0 + width; ++x) {
        if (pixelMatches(pixelAt(image, x, y), colour, 40)) {
          first = Math.min(first, x);
          last = Math.max(last, x);
        }
      }
    }
    return { first, last };
  };
  let plain = 0;
  let count = 0;
  for (let y = Math.floor(y0); y < y0 + height; y += 3) {
    for (let x = Math.floor(x0); x < x0 + width; x += 3) {
      plain += pixelMatches(pixelAt(image, x, y), background, 6) ? 1 : 0;
      ++count;
    }
  }
  return { green: columns(kGreen), purple: columns(kPurple), plainShare: plain / count };
};

// the colour most pixels of a band have that are not the background: the solid core of the glyphs drawn in it
const commonInk = (image, [x0, y0, width, height], background) => {
  const counts = new Map();
  for (let y = Math.floor(y0); y < y0 + height; ++y) {
    for (let x = Math.floor(x0); x < x0 + width; ++x) {
      const { r, g, b } = pixelAt(image, x, y);
      if (!pixelMatches({ r, g, b }, background, 24)) {
        const key = `${r},${g},${b}`;
        counts.set(key, (counts.get(key) ?? 0) + 1);
      }
    }
  }
  const [key] = [...counts.entries()].reduce((best, entry) => (entry[1] > best[1] ? entry : best), ["", 0]);
  const [r, g, b] = key.split(",").map(Number);
  return { r, g, b };
};

// the rows of the first line of text below the plot of the chart spanning columns `left`..`right`: the plot's lower
// edge is the last row mostly drawn across, and the tick labels are the first run of inked rows after a blank one
const tickLabelRows = (image, left, right, top, bottom, background, ink) => {
  const inked = (y, colour, delta) => {
    let count = 0;
    for (let x = left; x < right; ++x) {
      count += pixelMatches(pixelAt(image, x, y), colour, delta) ? 1 : 0;
    }
    return count;
  };
  let edge = -1;
  for (let y = top; y < bottom; ++y) {
    if (right - left - inked(y, background, 24) > 0.5 * (right - left)) {
      edge = y;
    }
  }
  let first = -1;
  let y = edge + 1;
  for (; y < bottom && first < 0; ++y) {
    if (inked(y, ink, 40) > 0 && inked(y - 1, ink, 40) === 0) {
      first = y;
    }
  }
  while (y < bottom && inked(y, ink, 40) > 0) {
    ++y;
  }
  return { first, last: y - 1 };
};

const copy = mkdtempSync(join(tmpdir(), "dashboard-"));
try {
  cpSync(directory, copy, { recursive: true });
  writeFileSync(join(copy, "default", "workflows", "two.grc"), twoCharts);
  const deck = join(copy, "default", "talk.md");
  let text = readFileSync(deck, "utf8");
  // the first live slide shows the two charts, in the full box its layout gives it
  const live = text.indexOf(":::gr4", text.indexOf("{#live}"));
  text = text.slice(0, live) + text.slice(live).replace("workflow: workflows/controlled-sines.grc", "workflow: workflows/two.grc").replace("aspect: 4:3\n", "legend: none\n");
  const timing = text.indexOf(":::gr4", text.indexOf("{#live-timing}"));
  text = text.slice(0, timing) + text.slice(timing).replace("workflow: workflows/functions.grc", "workflow: workflows/functions.grc\nstatus: here\nlegend: middle");
  writeFileSync(deck, text);

  await withViewer(copy, async ({ server, browser }) => {
    for (const scheme of ["light", "dark"]) {
      const page = await openPage(browser, `${server.origin}/index.html#view=live&step=0`);
      await page.command("Emulation.setEmulatedMedia", { features: [{ name: "prefers-color-scheme", value: scheme }] });
      await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
      await page.waitFor("document.getElementById('canvas') && window.gr4Zoom && (globalThis.gr4Location || '').includes('view=live&') ? 1 : false");
      await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 1100, y: 690 });
      let shot = "";
      for (let frame = 0; frame < 120; ++frame) {
        shot = (await page.command("Page.captureScreenshot", { format: "png" })).result.data;
      }
      const image = decodePng(Buffer.from(shot, "base64"));
      const region = JSON.parse(await page.evaluate("JSON.stringify(window.gr4Regions || {})")).charts;
      expect(region !== undefined, `${scheme}: the region is published`);
      if (region === undefined) {
        await page.close();
        continue;
      }
      const [x, , width] = region;
      const middle = x + width / 2;
      const background = pixelAt(image, 20, Math.floor(region[1] + region[3] / 2)); // the slide, left of the region
      const { green, purple, plainShare } = survey(image, region, background);
      const slack = 6; // pixels: where a line meets the plot border it is blended with it

      expect(green.first >= x && green.last <= middle + slack, `${scheme}: the left chart's signal stays in the left half, columns ${green.first}..${green.last} against ${x.toFixed(0)}..${middle.toFixed(0)}`);
      expect(purple.first >= middle - slack && purple.last <= x + width, `${scheme}: the right chart's signal stays in the right half, columns ${purple.first}..${purple.last} against ${middle.toFixed(0)}..${(x + width).toFixed(0)}`);
      expect(green.last - green.first > 0.6 * (width / 2) && purple.last - purple.first > 0.6 * (width / 2), `${scheme}: and each fills most of its half, ${green.last - green.first} and ${purple.last - purple.first} of ${(width / 2).toFixed(0)} columns`);
      // the first line of body text below the title, whose runs are published word by word
      const runs = JSON.parse(await page.evaluate("JSON.stringify(window.gr4TextRuns || [])"));
      const lineTop = runs.find((run) => run[1] > runs[0][1] + runs[0][3])?.[1];
      const line = runs.filter((run) => run[1] === lineTop);
      const bodyText = line.length === 0 ? null : [line[0][0], lineTop, line.at(-1)[0] + line.at(-1)[2] - line[0][0], line[0][3]];
      expect(bodyText !== null, `${scheme}: the slide's body text is published`);
      if (bodyText !== null) {
        const textColour = commonInk(image, bodyText, background);
        const axisColour = commonInk(image, [x, region[1], width * 0.06, region[3]], background); // the left chart's y labels
        expect(pixelMatches(axisColour, textColour, 8), `${scheme}: the axis labels take the slide's text colour, ${JSON.stringify(axisColour)} against ${JSON.stringify(textColour)}`);
        const labels = tickLabelRows(image, Math.floor(x + width * 0.08), Math.floor(middle - width * 0.02), Math.floor(region[1] + region[3] / 2), Math.floor(region[1] + region[3]), background, textColour);
        const pixelsPerPoint = 1.333; // at the 1280 x 720 this runs at
        const floorDigit = (12 * pixelsPerPoint * 1409) / (1854 + 434);
        const digit = labels.last - labels.first + 1;
        expect(labels.first > 0 && digit >= floorDigit - 1, `${scheme}: tick digits stand ${digit} px, no less than the 12 pt floor's ${floorDigit.toFixed(1)} px`);
      }
      // what is not a line, a tick, a label or the grid is the slide: no window, frame or plot background over it
      expect(plainShare > 0.8, `${scheme}: ${(100 * plainShare).toFixed(1)} % of the region is the slide's own background ${JSON.stringify(background)}`);
      await page.close();
    }

    const page = await openPage(browser, `${server.origin}/index.html#view=live-timing&step=0`);
    await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
    await page.waitFor("document.getElementById('canvas') && window.gr4Zoom && (globalThis.gr4Location || '').includes('view=live-timing&') ? 1 : false");
    await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 1100, y: 690 });
    let shot = "";
    for (let frame = 0; frame < 120; ++frame) {
      shot = (await page.command("Page.captureScreenshot", { format: "png" })).result.data;
    }
    const image = decodePng(Buffer.from(shot, "base64"));
    const region = JSON.parse(await page.evaluate("JSON.stringify(window.gr4Regions || {})")).timing;
    expect(region !== undefined, "status: the region is published");
    if (region !== undefined) {
      const [x, y, width, height] = region;
      const kAmber = { r: 255, g: 166, b: 0 };
      const amberRows = [];
      for (let row = Math.floor(y); row < y + height; ++row) {
        for (let column = Math.floor(x); column < x + width; ++column) {
          if (pixelMatches(pixelAt(image, column, row), kAmber, 20)) {
            amberRows.push(row);
            break;
          }
        }
      }
      // a strip of the region's foot: no taller than a tenth of it
      const foot = y + height * 0.9;
      expect(amberRows.length > 0 && amberRows.every((row) => row >= foot), `status: the warning is drawn in amber at the region's foot, rows ${amberRows[0]}..${amberRows.at(-1)} against ${foot.toFixed(0)}..${(y + height).toFixed(0)}`);
    }
    await page.close();

    // The session's status bar needs no graph: on the references slide a region with `widget: status` and no workflow
    // draws OpenDigitizer's bar -- its three level dots -- across the foot, from a log kept since launch.
    const references = await openPage(browser, `${server.origin}/index.html#view=references&step=0`);
    await references.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
    const bar = JSON.parse(await references.waitFor("(window.gr4Regions || {}).log ? JSON.stringify(window.gr4Regions.log) : false"));
    for (let frame = 0; frame < 30; ++frame) {
      await references.command("Page.captureScreenshot", { format: "png" });
    }
    const shown = decodePng(Buffer.from((await references.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
    const [bx, by, bw, bh] = bar;
    const background = pixelAt(shown, Math.floor(bx + bw / 2), Math.floor(by - 4));
    let inked = 0;
    for (let row = Math.floor(by); row < by + bh; ++row) {
      for (let column = Math.floor(bx); column < bx + 60; ++column) {
        inked += pixelMatches(pixelAt(shown, column, row), background, 24) ? 0 : 1;
      }
    }
    expect(by > 720 * 0.75 && inked > 20, `the references slide's foot carries the session's status bar, at row ${by}, ${inked} inked pixels where its dots are`);
    await references.close();
  });
} finally {
  rmSync(copy, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserDashboard: all checks passed" : `qa_BrowserDashboard: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

// A region's toolbar sits in the box its slide gives it and holds the workflow's controls, and the charts stay docked in
// their regions: `workflows/controlled-sines.grc` on `live`, and `modulated-sines.grc` with its toolbar on
// `live-graph` three slides on.
//
// What a toolbar's controls do is checked natively by qa_NativeToolbar: a browser's synthesised clicks do not
// reliably reach an ImGui widget, the viewer takes them for a click on the slide. Here the controls' sliders are found
// inside the toolbar's published box as the wide runs of non-background pixels -- the workflow declares five, and no
// play, pause or stop; a docked chart has no title bar; and the graph's scheduler state is the one the viewer
// publishes.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt, pixelMatches } from "../devtools/png.mjs";
import { createHash } from "node:crypto";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserToolbar.mjs <viewer-web directory>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const screenshot = async (page) => decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));

const digestOf = (image, [x0, y0, width, height]) => {
  const hash = createHash("md5");
  for (let y = Math.floor(y0); y < y0 + height; y += 2) {
    const start = (y * image.width + Math.floor(x0)) * 4;
    hash.update(image.rgba.subarray(start, start + Math.floor(width) * 4));
  }
  return hash.digest("hex");
};

// how many different pictures the region showed over two seconds
const picturesOver2s = async (page, region) => {
  const seen = new Set();
  const until = Date.now() + 2000;
  while (Date.now() < until) {
    seen.add(digestOf(await screenshot(page), region));
  }
  return seen.size;
};

// a window's title bar: a band at least eight rows tall near the top of the region, each row one unbroken run of a
// colour that is not the slide's across most of the region. A docked chart has none; one that came undocked carries
// its plot's name in one. The frames of charts side by side are single rows with gaps between them, and are not one.
const titleBarIn = (image, [x, y, width, height]) => {
  const background = pixelAt(image, Math.max(2, Math.floor(x) - 8), Math.floor(y + height / 2));
  const barRow = (row) => {
    let longest = 0;
    let run = 0;
    let previous = "";
    for (let column = Math.floor(x); column < x + width; ++column) {
      const pixel = pixelAt(image, column, row);
      const key = pixelMatches(pixel, background, 12) ? "" : `${pixel.r >> 3},${pixel.g >> 3},${pixel.b >> 3}`;
      run = key !== "" && key === previous ? run + 1 : key !== "" ? 1 : 0;
      previous = key;
      longest = Math.max(longest, run);
    }
    return longest > 0.8 * width;
  };
  let band = 0;
  for (let row = Math.floor(y); row < y + height * 0.25; ++row) {
    band = barRow(row) ? band + 1 : 0;
    if (band >= 8) {
      return row - 7;
    }
  }
  return -1;
};

const settle = async (page, frames = 40) => {
  for (let frame = 0; frame < frames; ++frame) {
    await page.command("Page.captureScreenshot", { format: "png" });
  }
};

// the scheduler state of the workflow's graph, as the viewer publishes it
const stateOf = async (page, workflow) => JSON.parse(await page.evaluate("JSON.stringify(window.gr4Graphs || {})"))[workflow];
const kSpectrumWorkflow = "workflows/modulated-sines.grc";

const regionOf = async (page, id) => JSON.parse(await page.evaluate("JSON.stringify(window.gr4Regions || {})"))[id];

// the sliders across the toolbar, row by row: runs of non-background pixels wider than any label, button or icon,
// counted once per toolbar row -- the rows that cross most of them
const slidersIn = (image, [x, y, width, height]) => {
  const background = pixelAt(image, Math.floor(x + width - 3), Math.floor(y + height - 3)); // the slide, below the rule
  const perRow = [];
  for (let row = Math.floor(y); row < y + height; ++row) {
    // wider than a label or a button, narrower than the rule the toolbar draws along its foot
    perRow.push(runsAlong(image, x, width, row, background).filter((run) => run.to - run.from >= 50 && run.to - run.from < 0.9 * width).length);
  }
  const sliders = [];
  for (let row = 0; row < perRow.length; ) { // a band of consecutive rows that cross sliders is one toolbar row
    if (perRow[row] === 0) {
      ++row;
      continue;
    }
    let most = 0;
    const first = row;
    for (; row < perRow.length && perRow[row] > 0; ++row) {
      most = Math.max(most, perRow[row]);
    }
    sliders.push({ rows: [Math.floor(y) + first, Math.floor(y) + row - 1], sliders: most });
  }
  return sliders;
};

// where the first slider of the toolbar's first row starts: three icon buttons before it would push it right
const firstSliderIn = (image, [x, y, width, height]) => {
  const background = pixelAt(image, Math.floor(x + width - 3), Math.floor(y + height - 3));
  for (let row = Math.floor(y); row < y + height; ++row) {
    const wide = runsAlong(image, x, width, row, background).filter((run) => run.to - run.from >= 50 && run.to - run.from < 0.9 * width);
    if (wide.length > 0) {
      return wide[0].from;
    }
  }
  return undefined;
};

const runsAlong = (image, x, width, row, background) => {
  const runs = [];
  let start = -1;
  for (let column = Math.floor(x); column < x + width; ++column) {
    const inked = !pixelMatches(pixelAt(image, column, row), background, 20);
    if (inked && start < 0) {
      start = column;
    } else if (!inked && start >= 0) {
      runs.push({ from: start, to: column - 1, y: row });
      start = -1;
    }
  }
  return runs.filter((run) => run.to - run.from >= 10); // a button, not part of an icon or a stray anti-aliased pixel
};


const step = async (page, key, toView) => {
  for (let press = 0; press < 10 && !(await page.evaluate("(globalThis.gr4Location || '')")).includes(`view=${toView}&`); ++press) {
    const before = await page.evaluate("(globalThis.gr4Location || '')");
    await page.press(...key);
    await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? 1 : false`);
  }
  await settle(page, 60);
};

await withViewer(directory, async ({ server, browser }) => {
  // walked to from `live`, as a talk would be: a backward key returns where the deck came from, and a deep link has
  // nowhere behind it but the start
  const kLeft = ["ArrowLeft", "ArrowLeft", 37];
  const kRight = ["ArrowRight", "ArrowRight", 39];
  const page = await openPage(browser, `${server.origin}/index.html#view=live&step=0`);
  await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
  await page.waitFor("document.getElementById('canvas') && window.gr4Zoom && (globalThis.gr4Location || '').includes('view=live&') ? 1 : false");
  await page.evaluate("document.getElementById('canvas').focus()");
  await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 1100, y: 690 });
  await step(page, kRight, "live-graph");
  await settle(page, 60);

  const charts = await regionOf(page, "dashboard");
  const toolbar = JSON.parse(await page.evaluate("JSON.stringify(window.gr4Toolbars || {})")).dashboard;
  expect(charts !== undefined && toolbar !== undefined, "the two-views slide publishes its charts and its toolbar");
  if (charts === undefined || toolbar === undefined) {
    return;
  }
  expect((await picturesOver2s(page, charts)) > 1, "the charts run before anything is pressed");

  const rows = slidersIn(await screenshot(page), toolbar);
  const sliders = rows.reduce((sum, row) => sum + row.sliders, 0);
  expect(sliders === 5, `the toolbar shows the workflow's five controls -- f1, A1, f2, A2 and noise -- found ${sliders} sliders, ${JSON.stringify(rows)}`);
  const lead = (firstSliderIn(await screenshot(page), toolbar) ?? Infinity) - toolbar[0];
  expect(lead <= 40, `and no play, pause or stop before them: f1's slider starts ${lead} px into the toolbar`);
  let titled = titleBarIn(await screenshot(page), charts);
  expect(titled < 0, `the charts are docked in their region, no title bar${titled < 0 ? "" : ` at row ${titled}`}`);

  await step(page, kLeft, "live");
  const elsewhere = await regionOf(page, "charts");
  expect(elsewhere !== undefined && (await picturesOver2s(page, elsewhere)) > 1 && (await stateOf(page, "workflows/controlled-sines.grc")) === "RUNNING", `the charts on \`live\` run, ${await stateOf(page, "workflows/controlled-sines.grc")}`);
  if (elsewhere !== undefined) {
    titled = titleBarIn(await screenshot(page), elsewhere);
    expect(titled < 0, `docked in that region too, no title bar${titled < 0 ? "" : ` at row ${titled}`}`);
  }

  await step(page, kRight, "live-graph");
  expect((await picturesOver2s(page, charts)) > 1 && (await stateOf(page, kSpectrumWorkflow)) === "RUNNING", `and on \`live-graph\` again, ${await stateOf(page, kSpectrumWorkflow)}`);
  titled = titleBarIn(await screenshot(page), charts);
  expect(titled < 0, `docked in its region again, no title bar${titled < 0 ? "" : ` at row ${titled}`}`);

  if (failures.length > 0) {
    console.error("console output:\n  " + page.consoleLines.slice(-20).join("\n  "));
  }
  await page.close();
});

console.log(failures.length === 0 ? "qa_BrowserToolbar: all checks passed" : `qa_BrowserToolbar: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

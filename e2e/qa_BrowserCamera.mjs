// The raster camera only exists once a picture is on the GPU and a frame of it has been chosen, so it is asserted
// against what the browser actually painted. The arithmetic of regions, aspect fitting and waypoints is covered by
// qa_RasterCamera; what is checked here is that the camera ends up looking at the thing the author named.
//
// Ground truth is what is in the picture, stated as claims a person can check by looking at it rather than as pixel
// values: Wally wears red and white stripes and nothing else the camera stops on does, the two ends of a tour frame
// the same whole scene, and the stops in between frame different parts of it. For the Solvay tour the regions the
// deck names are read from talk.md itself, so "the camera frames what the author named" is checked against the
// author's numbers rather than against anything the viewer computed. Absolute colours are avoided
// deliberately -- the same region covers a different number of source pixels at each zoom, so an average is only
// comparable with another average of the same thing, and every claim below is a relationship between stops.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt, describePixel } from "../devtools/png.mjs";
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserCamera.mjs <viewer-web directory>");
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

/// of the width: wide enough to clear the side menu, which a synthesised pointer does not reliably close
const kMenuEdge = 0.36;

/// the picture-only (`camera: image`) tour's steps by what each frames: the whole picture, three regions, the whole again
const kSlideTour = { plain: 0, stand: 1, statue: 2, wally: 3, out: 4 };
const slideStep = (step) => `view=search-slide&step=${step}`;

/// the Solvay tour's `:::stop` regions, as the author wrote them, in fractions of the picture; null is the whole picture
const tourRegions = (() => {
  const text = readFileSync(join(directory, "default", "talk.md"), "utf8");
  // a `:::stop` quoted in a code block, as the aside quotes one, is an example, not a stop
  const section = text.slice(text.indexOf("{#solvay}"), text.indexOf("\n# ", text.indexOf("{#solvay}"))).replace(/^```[^\n]*\n[\s\S]*?^```$/gm, "");
  return [...section.matchAll(/^:::stop(?: \{([^}]*)\})?$/gm)].map((match) => {
    const region = /region="([^"]+)"/.exec(match[1] ?? "");
    if (!region) return null;
    const [x, y, width, height] = region[1].split(/\s+/).map(Number);
    return { x, y, width, height };
  });
})();

/// waits for the cursor to reach `wanted` (a hash fragment) and the canvas to stop changing; returns the settled frame
const settledOn = async (page, key, wanted) => {
  for (let press = 0; press < 80; ++press) {
    const current = await page.evaluate("(globalThis.gr4Location || '')");
    if (current.includes(wanted)) break;
    await page.press(...key);
    await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(current)} ? (globalThis.gr4Location || '') : false`);
  }
  let previous = "";
  let stable = 0;
  for (let attempt = 0; attempt < 80 && stable < 3; ++attempt) {
    const shot = (await page.command("Page.captureScreenshot", { format: "png" })).result.data;
    stable = shot === previous ? stable + 1 : 0;
    previous = shot;
  }
  return decodePng(Buffer.from(previous, "base64"));
};

const kLeft = ["ArrowLeft", "ArrowLeft", 37];

const openDeck = async (browser, server) => {
  const page = await openPage(browser, `${server.origin}/index.html`);
  await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
  await page.waitFor("(globalThis.gr4Location || '').includes('view=') && (globalThis.gr4Location || '').includes('step=0')");
  await page.evaluate("document.getElementById('canvas').focus()");
  // parked at the left edge the pointer opens the side menu, which would sit over the picture
  await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 640, y: 360 });
  return page;
};

const published = async (page) => JSON.parse(await page.evaluate("JSON.stringify({ box: window.gr4InfoBox, frame: window.gr4CameraFrame })"));

/// a patch's average, over the screen pixels a rectangle covers, of how far two frames differ
const changedShare = (left, right, x0, y0, x1, y1) => {
  let changed = 0;
  let total = 0;
  for (let y = y0; y < y1; y += 2) {
    for (let x = x0; x < x1; x += 2) {
      total += 1;
      changed += apartness(pixelAt(left, x, y), pixelAt(right, x, y)) > 60 ? 1 : 0;
    }
  }
  return changed / total;
};

/// how much redder than the rest a colour is; Wally's stripes are the only strongly red thing the camera stops on
const redness = (pixel) => pixel.r - (pixel.g + pixel.b) / 2;

const apartness = (left, right) =>
  Math.abs(left.r - right.r) +
  Math.abs(left.g - right.g) +
  Math.abs(left.b - right.b);

/// bright enough to be part of the picture rather than the slide's near-black background
const isLit = (pixel) => pixel.r + pixel.g + pixel.b > 110;

/// the share of a row, across the middle of the screen, that is lit
const litShareOf = (image, y) => {
  let lit = 0;
  let total = 0;
  for (
    let x = Math.floor(image.width * kMenuEdge);
    x < image.width * 0.85;
    x += 4
  ) {
    total += 1;
    lit += isLit(pixelAt(image, x, y)) ? 1 : 0;
  }
  return lit / total;
};

/**
 * The picture's vertical extent: the rows most of which are lit.
 *
 * A single column will not do. The scene has dark trees and lamp posts down the middle that break the run in two,
 * and the midpoint of half a picture is not its centre. A line of prose lights only a fraction of its row, so the
 * threshold separates words from picture cleanly.
 */
const pictureRowsOf = (image) => {
  let top = -1;
  let bottom = -2;
  for (let y = 0; y < image.height; ++y) {
    if (litShareOf(image, y) <= 0.6) continue;
    if (top < 0) top = y;
    bottom = y;
  }
  return { top, bottom };
};

/// the first lit row above the picture, which is where the heading sits when the heading is not in the picture
const headingTopAbove = (image, limit) => {
  for (let y = 0; y < limit; ++y) {
    for (
      let x = Math.floor(image.width * 0.25);
      x < image.width * 0.75;
      x += 2
    ) {
      if (isLit(pixelAt(image, x, y))) {
        return y;
      }
    }
  }
  return -1;
};

/// the average colour of a patch: a single pixel of a hand-drawn scene lands on a brush stroke as often as on the
/// colour the area actually is
const averageAround = (image, cx, cy, half) => {
  let r = 0;
  let g = 0;
  let b = 0;
  let count = 0;
  for (
    let y = Math.max(cy - half, 0);
    y <= Math.min(cy + half, image.height - 1);
    ++y
  ) {
    for (
      let x = Math.max(cx - half, 0);
      x <= Math.min(cx + half, image.width - 1);
      ++x
    ) {
      const pixel = pixelAt(image, x, y);
      r += pixel.r;
      g += pixel.g;
      b += pixel.b;
      count += 1;
    }
  }
  return {
    r: Math.round(r / count),
    g: Math.round(g / count),
    b: Math.round(b / count),
    a: 255,
  };
};

const middleOfPicture = (image) => {
  const run = pictureRowsOf(image);
  return {
    run,
    pixel: averageAround(
      image,
      Math.floor(image.width / 2),
      Math.floor((run.top + run.bottom) / 2),
      24,
    ),
  };
};


await withViewer(directory, async ({ server, browser }) => {
  const page = await openDeck(browser, server);
  const frames = new Map();
  const middles = new Map();
  const boxes = new Map();
  for (const [name, step] of Object.entries(kSlideTour)) {
    const image = await settledOn(page, kRight, slideStep(step));
    frames.set(name, image);
    boxes.set(name, (await published(page)).box);
    const middle = middleOfPicture(image);
    middles.set(name, middle);
    expect(middle.run.bottom - middle.run.top > image.height / 4, `search-slide step ${step} (${name}): the picture is on screen, rows ${middle.run.top}..${middle.run.bottom}`);
  }

  // Wally is the only stop whose middle is red: he is behind the tree in red and white stripes
  const wallyRed = redness(middles.get("wally").pixel);
  expect(wallyRed > 25, `the Wally stop frames the red and white stripes, redness ${wallyRed.toFixed(1)} of ${describePixel(middles.get("wally").pixel)}`);
  for (const name of ["stand", "statue"]) {
    expect(redness(middles.get(name).pixel) < wallyRed - 15, `the ${name} stop does not, redness ${redness(middles.get(name).pixel).toFixed(1)}`);
  }
  // the whole square has warm brick at its middle (redness 32 measured), so it is only claimed to be less red than Wally
  expect(redness(middles.get("plain").pixel) < wallyRed, `the whole square is less red than Wally, redness ${redness(middles.get("plain").pixel).toFixed(1)}`);
  for (const [name, left, right] of [
    ["the stand and the statue", middles.get("stand").pixel, middles.get("statue").pixel],
    ["the statue and Wally", middles.get("statue").pixel, middles.get("wally").pixel],
  ]) {
    expect(apartness(left, right) > 40, `${name} are framed differently, ${apartness(left, right)} apart`);
  }
  const wideMiddle = (image) => averageAround(image, 640, Math.floor((pictureRowsOf(image).top + pictureRowsOf(image).bottom) / 2), 120);
  expect(apartness(wideMiddle(frames.get("plain")), wideMiddle(frames.get("out"))) < 10, `the picture-only tour ends on the picture it began with`);

  // under `camera: image` only the picture moves: the heading keeps its rows at every stop, and the picture stays
  // below it, in its box
  const kHeadingBottom = 45;
  for (const name of ["stand", "statue", "wally"]) {
    const moved = changedShare(frames.get("plain"), frames.get(name), 30, 8, 520, 40);
    expect(moved < 0.02 && middles.get(name).run.top > kHeadingBottom, `the ${name} stop leaves the heading where it was (${(moved * 100).toFixed(1)} % of its band changed) and the picture below it, from row ${middles.get(name).run.top}`);
  }
  expect([...boxes.values()].every((box) => box.length === 0), `the picture-only tour has no boxes`);

  if (failures.length > 0) {
    console.error("console output:\n  " + page.consoleLines.join("\n  "));
  }
  await page.close();
});

// The Solvay tour: one section, one picture, a camera stop per key. Each stop is checked against the region the
// author wrote for it, the whole slide is magnified (the heading carried off), its box sits beside the face without covering
// it, and the tour ends on the frame it began with.
let tourFrames = [];
let tourState = [];
await withViewer(directory, async ({ server, browser }) => {
  const page = await openDeck(browser, server);
  expect(tourRegions.length === 8 && tourRegions.slice(0, 7).every(Boolean) && tourRegions[7] === null, `the deck's tour has seven regions and an empty last stop, found ${tourRegions.length}`);
  for (let step = 0; step <= tourRegions.length; ++step) {
    const image = await settledOn(page, kRight, `view=solvay&step=${step}`);
    tourFrames.push(image);
    tourState.push(await published(page));
  }
  const kHeadingBottom = 45;
  const opening = tourFrames[0];
  const openingRows = pictureRowsOf(opening);
  expect(tourState[0].box.length === 0, `the tour opens on the whole slide with no box`);
  expect(headingTopAbove(opening, kHeadingBottom) >= 0 && openingRows.top > kHeadingBottom, `with the title above the picture, picture from row ${openingRows.top}`);

  for (let step = 1; step <= 7; ++step) {
    const { box, frame } = tourState[step];
    const region = tourRegions[step - 1];
    const [bx, by, bw, bh, fx, fy, fw, fh] = box;
    const holds = frame[0] <= region.x + 1e-3 && frame[1] <= region.y + 1e-3 && frame[0] + frame[2] >= region.x + region.width - 1e-3 && frame[1] + frame[3] >= region.y + region.height - 1e-3;
    expect(holds && frame[2] < 0.35, `stop ${step}: the camera frames the region the deck names, ${JSON.stringify(region)} inside ${JSON.stringify(frame)}`);
    expect(box.length === 8 && bx >= 0 && by >= 0 && bx + bw <= 1280 && by + bh <= 720, `stop ${step}: its box is wholly on screen, ${JSON.stringify(box)}`);
    const overlaps = bx < fx + fw && fx < bx + bw && by < fy + fh && fy < by + bh;
    const besideFace = bx + bw <= fx || bx >= fx + fw || by + bh <= fy || by >= fy + fh;
    expect(!overlaps || besideFace, `stop ${step}: and beside the face, not over it, box ${bx}..${bx + bw} face ${fx}..${fx + fw}`);
    // the title's place now holds the picture: the band the heading occupied looks nothing like it did
    const titleGone = changedShare(opening, tourFrames[step], 30, 8, 520, 40);
    expect(titleGone > 0.4, `stop ${step}: the title has stepped aside, ${(titleGone * 100).toFixed(0)} % of its band changed`);
    if (step > 1) {
      const previous = tourState[step - 1].frame;
      expect(Math.abs(previous[0] - frame[0]) + Math.abs(previous[1] - frame[1]) > 0.02, `stop ${step}: the camera moved from the stop before, ${JSON.stringify(previous)} to ${JSON.stringify(frame)}`);
      // each box carries its own stop's words, so the box's ink differs from the last one's
      const [px, py, pw, ph] = tourState[step - 1].box;
      const x0 = Math.max(bx, px);
      const y0 = Math.max(by, py);
      const x1 = Math.min(bx + bw, px + pw);
      const y1 = Math.min(by + bh, py + ph);
      if (x1 > x0 && y1 > y0) {
        expect(changedShare(tourFrames[step - 1], tourFrames[step], x0, y0, x1, y1) > 0.05, `stop ${step}: and its box says something other than the last one's`);
      }
    }
  }

  const closing = tourFrames[8];
  const sameFrame = tourState[8].frame.every((value, index) => Math.abs(value - tourState[0].frame[index]) < 1e-3);
  expect(sameFrame && tourState[8].box.length === 0, `the empty last stop is the whole slide again, without a box`);
  const wideMiddle = (image) => averageAround(image, 640, Math.floor((pictureRowsOf(image).top + pictureRowsOf(image).bottom) / 2), 120);
  expect(apartness(wideMiddle(opening), wideMiddle(closing)) < 10 && headingTopAbove(closing, kHeadingBottom) >= 0, `and ends on the frame it began with, ${describePixel(wideMiddle(opening))} and ${describePixel(wideMiddle(closing))}`);

  // stepping back retraces the tour
  await settledOn(page, kLeft, "view=solvay&step=7");
  const back = await published(page);
  const apart = back.frame.reduce((sum, value, index) => sum + Math.abs(value - tourState[7].frame[index]), 0);
  expect(apart < 0.005 && back.box.length === 8, `stepping back returns to the last stop with its box, frames ${apart.toFixed(4)} apart`);

  if (failures.length > 0) {
    console.error("console output:\n  " + page.consoleLines.slice(-20).join("\n  "));
  }
  await page.close();
});

// The same tours on a phone held upright and on a wide screen: every box is on screen and beside what it describes,
// and an image-scope stop still frames the region the deck names. On the phone no side has room, so the box goes
// above or below at most of the screen's width rather than squeezing into a column of single words.
for (const [width, height] of [[390, 844], [1600, 700]]) {
  await withViewer(directory, async ({ server, browser }) => {
    const page = await openPage(browser, "about:blank");
    await page.command("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor: 1, mobile: false });
    await page.command("Page.navigate", { url: `${server.origin}/index.html` });
    await page.waitFor("document.getElementById('canvas') && (globalThis.gr4Location || '').includes('view=') && (globalThis.gr4Location || '').includes('step=0') ? 1 : false");
    await page.evaluate("document.getElementById('canvas').focus()");
    await settledOn(page, kRight, slideStep(kSlideTour.wally));
    const { box: wallyBox } = await published(page);
    expect(wallyBox.length === 0, `${width}x${height}: the picture-only Wally stop has no box`);
    for (const [wanted, region] of [["view=solvay&step=1", tourRegions[0]], ["view=solvay&step=3", tourRegions[2]]]) {
      await settledOn(page, kRight, wanted);
      const { box, frame } = await published(page);
      const [bx, by, bw, bh, fx, fy, fw, fh] = box;
      const onScreen = box.length === 8 && bx >= 0 && by >= 0 && bx + bw <= width && by + bh <= height;
      const besideFace = bx + bw <= fx || bx >= fx + fw || by + bh <= fy || by >= fy + fh;
      expect(onScreen && besideFace, `${width}x${height} ${wanted}: the box is on screen and beside what it describes, ${JSON.stringify(box)}`);
      if (width < height) {
        expect(bw >= width * 0.6, `${width}x${height} ${wanted}: with no room beside, the box takes most of the width, ${bw} px`);
      }
      if (region) {
        const holds = frame[0] <= region.x + 1e-3 && frame[1] <= region.y + 1e-3 && frame[0] + frame[2] >= region.x + region.width - 1e-3 && frame[1] + frame[3] >= region.y + region.height - 1e-3;
        expect(holds, `${width}x${height} ${wanted}: the camera frames the region the deck names, inside ${JSON.stringify(frame)}`);
      }
    }
    await page.close();
  });
}

// A long jump pulls back, travels and pushes in, taking the three seconds the stop asks for; the move from the
// opening slide into the first stop fades the words out on the way. The published frame is read as fast as the page
// answers, which is far finer than a screenshot.
await withViewer(directory, async ({ server, browser }) => {
  const page = await openDeck(browser, server);
  await settledOn(page, kRight, "view=solvay&step=0");
  const pressed = Date.now();
  await page.press(...kRight);
  const widths = [];
  while (Date.now() - pressed < 2000) {
    widths.push((await published(page)).frame[2]);
  }
  const between = widths.filter((value) => value < widths[0] - 0.02 && value > widths[widths.length - 1] + 0.02).length;
  expect(between > 3, `the slide zooms in smoothly rather than cutting, ${between} frames between the whole slide and the stop`);

  await settledOn(page, kRight, "view=solvay&step=2");
  const start = (await published(page)).frame;
  const sampleMove = async (key) => {
    const pressedAt = Date.now();
    await page.press(...key);
    const samples = [];
    while (Date.now() - pressedAt < 5000) {
      const now = await published(page);
      samples.push({ at: Date.now() - pressedAt, frame: now.frame, box: now.box });
    }
    const end = samples[samples.length - 1].frame;
    const lastMoving = samples.filter((each) => each.frame.some((value, index) => Math.abs(value - end[index]) > 1e-3)).pop();
    return { samples, end, widest: Math.max(...samples.map((each) => each.frame[2])), lastMoving };
  };
  const forwards = await sampleMove(kRight);
  expect(forwards.widest > 0.8 && start[2] < 0.35 && forwards.end[2] < 0.35, `Curie to Bohr pulls back to the whole picture and pushes in again, frame width ${start[2]} -> ${forwards.widest} -> ${forwards.end[2]}`);
  expect(forwards.lastMoving !== undefined && forwards.lastMoving.at > 2600 && forwards.lastMoving.at < 3500, `and the move takes the three seconds the stop asks for, last change at ${forwards.lastMoving?.at} ms`);
  // the box waits for the camera: none while the frame still moves, and Bohr's once it has stopped
  // counted from the first sample the move shows in, since the page answers a few samples after the key is sent
  const firstMoving = forwards.samples.find((each) => each.frame.some((value, index) => Math.abs(value - start[index]) > 1e-3));
  const movingWithBox = forwards.samples.filter((each) => each.at >= (firstMoving?.at ?? 0) && each.at <= (forwards.lastMoving?.at ?? 0) && each.box.length > 0).length;
  expect(movingWithBox === 0 && forwards.samples[forwards.samples.length - 1].box.length === 8, `no box is shown during the move, ${movingWithBox} samples had one, and one is shown after it`);

  const backwards = await sampleMove(kLeft);
  expect(backwards.widest > 0.8 && backwards.end[2] < 0.35, `stepping back from Bohr retraces the pull-back, frame width up to ${backwards.widest}`);
  expect(backwards.lastMoving !== undefined && backwards.lastMoving.at > 2600 && backwards.lastMoving.at < 3500, `at the same three-second pace, last change at ${backwards.lastMoving?.at} ms`);
  await page.close();
});

// Under `camera: slide` the move carries the words with it: on Solvay's opening slide the picture starts below the
// heading and, once the slide has been magnified onto Bragg, covers the rows the heading had.
await withViewer(directory, async ({ server, browser }) => {
  const page = await openDeck(browser, server);
  const start = await settledOn(page, kRight, "view=solvay&step=0");
  const pressed = Date.now();
  await page.press(...kRight);
  const samples = [];
  while (Date.now() - pressed < 4000) {
    const shot = (await page.command("Page.captureScreenshot", { format: "png" })).result.data;
    samples.push(decodePng(Buffer.from(shot, "base64")));
  }
  const last = samples[samples.length - 1];
  expect(pictureRowsOf(start).top > 110, `the picture starts below the heading as the move to the slide-scope stop begins, row ${pictureRowsOf(start).top}`);
  expect(pictureRowsOf(last).top < 10, `and the magnified slide has carried the heading off the top, leaving the picture from row ${pictureRowsOf(last).top}`);
  await page.close();
});

// The info box's backdrop is the deck's background at 70 % over the photograph. Ground truth is the photograph
// itself, as the same stop paints it with no words: the deck is copied with the first stop's lines removed, so the
// box is absent and the pixel under where it was is the photograph's own; the background is the opening slide's.
// A slide-scope stop that names no region is an image-scope stop: with nothing to magnify the glass changes nothing.
// The deck is copied with `camera: slide` removed from that stop, and the two frames must be the same bytes.
{
  const copy = mkdtempSync(join(tmpdir(), "camera-identity-"));
  try {
    cpSync(directory, copy, { recursive: true });
    const deck = join(copy, "default", "talk.md");
    let text = readFileSync(deck, "utf8");
    const plainStop = text.indexOf(":::layout", text.indexOf("{#solvay}"));
    const scopeLine = text.indexOf("camera: slide\n", plainStop);
    text = text.slice(0, scopeLine) + text.slice(scopeLine + "camera: slide\n".length);
    // the identity is compared on a deck with nothing else changed: a problem reported by the stop added below shows
    // in the menu, and the menu is on screen
    const plain = join(copy, "plain");
    cpSync(join(copy, "default"), join(plain, "default"), { recursive: true });
    cpSync(join(directory), plain, { recursive: true, filter: (source) => !source.includes(`${join(directory, "default")}`) });
    writeFileSync(join(plain, "default", "talk.md"), text);
    // after the first stop, the same region again with no words, so the frame is the same and the box is gone; and
    // before the last, a stop the viewer cannot read, which it must name rather than skip
    const firstStop = text.indexOf(":::stop {", text.indexOf("{#solvay}"));
    const firstFence = text.slice(firstStop, text.indexOf("\n", firstStop));
    // the next stop starts after a blank line; the first stop's own body holds an aside and a quoted fence
    const secondStop = text.indexOf("\n\n:::stop", firstStop) + 2;
    text = text.slice(0, secondStop) + firstFence + "\n:::\n\n" + text.slice(secondStop);
    const lastStop = text.indexOf(":::stop\n:::", text.indexOf("{#solvay}"));
    text = text.slice(0, lastStop) + ':::stop {region="bad" box=middle}\n:::\n\n' + text.slice(lastStop);
    // and at the very end, a box whose fence is never closed, which must be named rather than swallow quietly
    text += "\n:::left\nthis box is never closed\n";
    writeFileSync(deck, text);

    for (const scheme of ["light", "dark"]) {
      await withViewer(copy, async ({ server, browser }) => {
        // the scheme is read once, as the viewer starts, so it is set on a blank page before the deck loads into it
        const page = await openPage(browser, "about:blank");
        await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
        await page.command("Emulation.setEmulatedMedia", { features: [{ name: "prefers-color-scheme", value: scheme }] });
        await page.command("Page.navigate", { url: `${server.origin}/index.html` });
        await page.waitFor("document.getElementById('canvas') && (globalThis.gr4Location || '').includes('view=') && (globalThis.gr4Location || '').includes('step=0') ? 1 : false");
        await page.evaluate("document.getElementById('canvas').focus()");
        await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 640, y: 360 });
        const opening = await settledOn(page, kRight, "view=solvay&step=0");
        const boxed = await settledOn(page, kRight, "view=solvay&step=1");
        const [bx, by, bw, bh] = (await published(page)).box;
        const bare = await settledOn(page, kRight, "view=solvay&step=2");
        const spot = { x: bx + bw - 7, y: by + bh - 7 };
        const photo = averageAround(bare, spot.x, spot.y, 1);
        const mixed = averageAround(boxed, spot.x, spot.y, 1);
        const background = averageAround(opening, 1270, 360, 1);
        const kBackdrop = 0.7; // the author's choice: 30 % transparent
        const expected = { r: photo.r * (1 - kBackdrop) + background.r * kBackdrop, g: photo.g * (1 - kBackdrop) + background.g * kBackdrop, b: photo.b * (1 - kBackdrop) + background.b * kBackdrop };
        const off = Math.max(Math.abs(mixed.r - expected.r), Math.abs(mixed.g - expected.g), Math.abs(mixed.b - expected.b));
        expect(off <= 6, `${scheme}: the box's backdrop is the background at 70 % over the photograph, ${describePixel(mixed)} against ${describePixel(photo)} and ${describePixel(background)} mixed, ${off.toFixed(1)} levels off`);
        if (scheme === "dark") {
          const named = page.consoleLines.filter((line) => line.includes("solvay/stop"));
          expect(named.some((line) => line.includes("region 'bad'")) && named.some((line) => line.includes("box 'middle'")), `an unreadable stop is named in the problems list, ${named.length} lines`);
          const unclosed = page.consoleLines.filter((line) => line.includes(":::left") && line.includes("never closed"));
          expect(unclosed.length > 0, `a box whose fence is never closed is named in the problems list, ${unclosed.length} lines`);
        }
        await page.close();
      });
    }

    await withViewer(plain, async ({ server, browser }) => {
      const page = await openDeck(browser, server);

      const imageScope = await settledOn(page, kRight, "view=solvay&step=0");
      const kRenderNoise = 4;
      let largest = 0;
      let differing = 0;
      const where = { x0: Infinity, y0: Infinity, x1: -1, y1: -1 };
      // The picture's rows and a 2 px margin are compared. Text is left out: glyphs come out differently by a few
      // levels in about half of all page loads, in control runs of one unchanged deck as well (the heading, and now
      // the caption under the picture), so it says nothing about the camera.
      const rows = pictureRowsOf(imageScope);
      for (let y = Math.max(rows.top - 2, 0); y <= Math.min(rows.bottom + 2, imageScope.height - 1); ++y) {
        for (let x = 0; x < imageScope.width; ++x) {
          const left = pixelAt(tourFrames[0], x, y);
          const right = pixelAt(imageScope, x, y);
          const level = Math.max(Math.abs(left.r - right.r), Math.abs(left.g - right.g), Math.abs(left.b - right.b));
          largest = Math.max(largest, level);
          if (level > kRenderNoise) {
            ++differing;
            where.x0 = Math.min(where.x0, x); where.y0 = Math.min(where.y0, y); where.x1 = Math.max(where.x1, x); where.y1 = Math.max(where.y1, y);
          }
        }
      }
      expect(differing === 0, `a slide-scope stop with no region is pixel-identical to an image-scope one, ${differing} pixels differ by more than ${kRenderNoise} levels (largest ${largest}) within x ${where.x0}..${where.x1}, y ${where.y0}..${where.y1}`);
      await page.close();
    });
  } finally {
    rmSync(copy, { recursive: true, force: true });
  }
}

process.exit(failures.length === 0 ? 0 : 1);

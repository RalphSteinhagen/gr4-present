// A grid's arithmetic is covered by qa_Grid; what cannot be checked there is whether boxes actually become columns
// on a screen. This drives the deck's grid slide and asserts that its row really is in separated columns, and that
// the title sits where it sits on every other slide.
//
// Everything left of kMenuEdge is ignored. The side menu slides out when the pointer is near the left edge and a
// synthesised pointer does not reliably leave it, so the strip it occupies is not evidence either way -- and the
// columns being measured are well clear of it.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt } from "../devtools/png.mjs";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserGrid.mjs <viewer-web directory>");
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
const isLit = (pixel) => pixel.r + pixel.g + pixel.b > 110;
const kMenuEdge = 0.36; // of the width: wide enough to clear the side menu, which is sized in ems and so is not a fixed share
const kTitleLeft = 30; // px: the slide's left margin; measured only once the side menu is known to be closed
const kCellsLeft = 540; // px: right of the grid slide's syntax panel, whose code line splits into many word runs
const kWordGap = 18; // px: wider than the space between words, narrower than the gutter between boxes

/// the columns of the image that carry any text between two rows, as runs of lit columns
const textRunsBetween = (image, fromY, toY) => {
  const runs = [];
  let start = -1;
  for (let x = 0; x < image.width; ++x) {
    let lit = false;
    for (let y = fromY; y < toY && !lit; ++y) {
      lit = isLit(pixelAt(image, x, y));
    }
    if (lit && start < 0) {
      start = x;
    } else if (!lit && start >= 0) {
      // a gap of a few pixels is the space between words, not between columns
      if (runs.length > 0 && start - runs[runs.length - 1].to < kWordGap) {
        runs[runs.length - 1].to = x - 1;
      } else {
        runs.push({ from: start, to: x - 1 });
      }
      start = -1;
    }
  }
  return runs;
};

const litRow = (image, y, fromX = Math.floor(image.width * kMenuEdge)) => {
  for (
    let x = fromX;
    x < image.width * 0.9;
    x += 2
  ) {
    if (isLit(pixelAt(image, x, y))) {
      return true;
    }
  }
  return false;
};

/**
 * The height of the heading's first line of glyphs.
 *
 * This is what has to be the same from slide to slide: the size of the title, not its position. A heading that
 * wraps to two lines sits lower than one that does not, and that is fine; a heading drawn smaller because this
 * slide's happens to be longer is not, and that is what used to happen.
 *
 * What is measured is ink, not type: a line with a descender in it covers a few more rows than one without, so
 * the two are compared with a tolerance rather than for equality. A heading drawn at the wrong size differs by
 * far more than a descender does.
 */
const titleLineHeight = (image) => {
  let top = -1;
  for (let y = 0; y < image.height && top < 0; ++y) {
    if (litRow(image, y, kTitleLeft)) {
      top = y;
    }
  }
  if (top < 0) {
    return -1;
  }
  let bottom = top;
  while (bottom + 1 < image.height && litRow(image, bottom + 1, kTitleLeft)) {
    ++bottom;
  }
  return bottom - top + 1;
};

await withViewer(directory, async ({ server, browser }) => {
  const page = await openPage(browser, `${server.origin}/index.html`);
  await page.command("Emulation.setDeviceMetricsOverride", {
    width: 1280,
    height: 720,
    deviceScaleFactor: 1,
    mobile: false,
  });
  await page.waitFor(
    "(globalThis.gr4Location || '').includes('view=') && (globalThis.gr4Location || '').includes('step=0')",
  );
  await page.evaluate("document.getElementById('canvas').focus()");
  // the pointer near the left edge opens the side menu, which would sit over the left column
  for (let settle = 0; settle < 6; ++settle) {
    await page.command("Input.dispatchMouseEvent", {
      type: "mouseMoved",
      x: 1100,
      y: 600,
    });
    await page.command("Page.captureScreenshot", { format: "png" });
  }

  const settledAt = async (view) => {
    for (let step = 0; step < 60; ++step) {
      const current = await page.evaluate("(globalThis.gr4Location || '')");
      if (current.includes(`view=${view}&`)) break;
      await page.press(...kRight);
      await page.waitFor(
        `(globalThis.gr4Location || '') !== ${JSON.stringify(current)} ? (globalThis.gr4Location || '') : false`,
      );
    }
    // the side menu closed, so the title can be measured from the left margin: a synthesised key press leaves the
    // pointer at the window's corner, where it opens the menu, so the pointer is moved away first
    await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 1100, y: 600 });
    await page.waitFor("JSON.stringify(window.gr4MenuEntries || []) === '[]' ? 1 : false");
    let previous = "";
    let stable = 0;
    for (let attempt = 0; attempt < 80 && stable < 3; ++attempt) {
      const shot = (
        await page.command("Page.captureScreenshot", { format: "png" })
      ).result.data;
      stable = shot === previous ? stable + 1 : 0;
      previous = shot;
    }
    return decodePng(Buffer.from(previous, "base64"));
  };

  const single = await settledAt("intro"); // one box: prose, then its figures
  const grid = await settledAt("grid");

  // in the first lines below the 36 pt title, cells a and b beside the syntax panel read as separated columns
  const runs = textRunsBetween(
    grid,
    Math.floor(grid.height * 0.17),
    Math.floor(grid.height * 0.27),
  ).filter((run) => run.from >= kCellsLeft && run.to - run.from > 3); // a box's frame is a 1-2 px line, not a column of text
  expect(
    runs.length === 2,
    `the two cells are two separated columns, found ${runs.length}: ${JSON.stringify(runs)}`,
  );
  if (runs.length === 2) {
    const [left, right] = runs;
    expect(
      right.from - left.to > 30,
      `with a gutter between a and b, ${right.from - left.to} px`,
    );
    const widths = [left.to - left.from, right.to - right.from];
    expect(
      Math.abs(widths[0] - widths[1]) < Math.max(...widths) * 0.35,
      `a and b of comparable width for their equal split, ${widths[0]} and ${widths[1]}`,
    );
  }

  // a one-box slide is one column, or the check above would pass on anything
  const plain = textRunsBetween(
    single,
    Math.floor(single.height * 0.17),
    Math.floor(single.height * 0.27),
  );
  expect(
    plain.length === 1,
    `a single-box slide is one column, found ${plain.length}`,
  );

  // and the heading is drawn at one size whichever layout is below it and however long this slide's is
  const plainTitle = titleLineHeight(single);
  const gridTitle = titleLineHeight(grid);
  expect(
    plainTitle > 10 && Math.abs(plainTitle - gridTitle) <= 6,
    `the title is the same size across layouts, ${plainTitle} and ${gridTitle} pixels tall`,
  );

  if (failures.length > 0) {
    console.error("console output:\n  " + page.consoleLines.join("\n  "));
  }
  await page.close();
});

process.exit(failures.length === 0 ? 0 : 1);

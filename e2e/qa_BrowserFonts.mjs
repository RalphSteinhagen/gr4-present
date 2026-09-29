// The demo deck's "Fonts and sizes" slide, as the browser draws it: a box in the deck's own face, a few words in the
// body face, and words at their own size.
//
// Ground truth is each face's own metrics, read from the font files with fontTools: ImGui sets a face so its ascent
// and descent span the pixel size, so a word's width over its height is its advance widths over (ascent - descent).
// "written " is 2.0975 of that in Patrick Hand (ascent 1042, descent 312) and would be 2.8851 in Liberation Sans;
// "body " is 2.1897 in Liberation Sans and would be 1.3722 in Patrick Hand; "by " is 0.7866 in Patrick Hand and
// would be 1.1941 in Liberation Sans. Sizes are the deck's points: 1 pt is
// 1.333 px at 1280 x 720, so 12 pt is 16 px, and 150 % is half as tall again as the words around it.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserFonts.mjs <viewer-web directory>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const near = (value, wanted, tolerance) => Math.abs(value - wanted) <= tolerance;

await withViewer(directory, async ({ server, browser }) => {
  const page = await openPage(browser, `${server.origin}/index.html#view=fonts&step=0`);
  await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
  await page.waitFor("window.gr4TextRuns && window.gr4TextRuns.some((run) => run[4].startsWith('written')) ? 1 : false");
  const runs = JSON.parse(await page.evaluate("JSON.stringify(window.gr4TextRuns)"));
  const run = (word) => runs.find((candidate) => candidate[4] === word);
  // the lowest on the slide: the Markdown above each row quotes the same words in its code panel
  const lowest = (matches) => runs.filter(matches).sort((a, b) => b[1] - a[1])[0];

  const written = run("written ");
  expect(written !== undefined && near(written[2] / written[3], 2.0975, 0.08), `the box is set in Patrick Hand: "written " is ${written ? (written[2] / written[3]).toFixed(3) : "missing"} of its height wide, 2.0975 in that face`);
  const body = lowest((candidate) => candidate[4] === "body ");
  expect(body !== undefined && near(body[2] / body[3], 2.1897, 0.08), `the row is set in Liberation Sans: "body " is ${body ? (body[2] / body[3]).toFixed(3) : "missing"}, 2.1897 in that face`);
  const byHand = lowest((candidate) => candidate[4] === "by ");
  expect(byHand !== undefined && near(byHand[2] / byHand[3], 0.7866, 0.05), `[by hand]{font=hand} is set in Patrick Hand: "by " is ${byHand ? (byHand[2] / byHand[3]).toFixed(3) : "missing"}, 0.7866 in that face`);

  const box = lowest((candidate) => candidate[4] === "In ");
  const larger = lowest((candidate) => candidate[4].startsWith("half"));
  expect(box !== undefined && larger !== undefined && near(larger[3] / box[3], 1.5, 0.05), `size=150% is half as tall again: ${larger?.[3]} px against ${box?.[3]} px`);
  const smaller = lowest((candidate) => candidate[4].startsWith("at "));
  expect(smaller !== undefined && near(smaller[3], 16, 1), `size=12pt is 16 px at 1280 x 720: ${smaller?.[3]} px`);

  const problems = page.consoleLines.filter((line) => /font|face lacks/i.test(line) && /warn|error/i.test(line));
  expect(problems.length === 0, `the deck's faces load without a problem: ${problems.slice(0, 3).join(" | ") || "none"}`);
  await page.close();
});

console.log(failures.length === 0 ? "qa_BrowserFonts: all checks passed" : `qa_BrowserFonts: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

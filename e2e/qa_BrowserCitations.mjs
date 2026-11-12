// A citation mark shows the works it cites while the pointer rests on it, and a click leads to the references slide
// that lists them. A fingertip cannot hover: its first tap shows the works, a second on the same mark leads there.
// Ground truth: the shipped deck's `markdown` slide cites [^harris] and [^cooley], listed on its `references` slide.

import { kHeadlessQuery, openPage, withViewer } from "../devtools/browser_harness.mjs";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserCitations.mjs <viewer-web directory>");
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
const kCitations = "JSON.stringify((window.gr4Links || []).filter((link) => link[4].startsWith('cite:')))";
const centre = (link) => ({ x: link[0] + link[2] / 2, y: link[1] + link[3] / 2 });
const opened = async (page) => {
  await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
  await page.waitFor(`(globalThis.gr4Location || '').includes('view=markdown') && ${kCitations} !== '[]' ? 1 : false`);
  return JSON.parse(await page.evaluate(kCitations));
};
const until = async (page, condition) => page.waitFor(`${condition} ? 1 : false`, 10_000).catch(() => false);

await withViewer(directory, async ({ server, browser }) => {
  const page = await openPage(browser, `${server.origin}/index.html?${kHeadlessQuery}#view=markdown`);
  const marks = await opened(page);
  expect(marks.length >= 2, `the slide's citation marks lead somewhere: ${marks.map((mark) => mark[4]).join(", ")}`);

  const mark = centre(marks[0]);
  await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: mark.x, y: mark.y });
  expect(await until(page, `globalThis.gr4Citation === ${JSON.stringify(marks[0][4])}`), `resting on ${marks[0][4]} shows its work`);
  await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: mark.x, y: mark.y + 200 });
  expect(await until(page, "globalThis.gr4Citation === ''"), "moving away hides it");

  await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: mark.x, y: mark.y });
  await nextFrame();
  for (const type of ["mousePressed", "mouseReleased"]) {
    await page.command("Input.dispatchMouseEvent", { type, x: mark.x, y: mark.y, button: "left", clickCount: 1 });
    await nextFrame();
  }
  expect(await until(page, "(globalThis.gr4Location || '').includes('view=references')"), "a click leads to the references slide");
  await page.close();

  const touching = await openPage(browser, `${server.origin}/index.html?${kHeadlessQuery}#view=markdown`);
  await touching.command("Emulation.setTouchEmulationEnabled", { enabled: true, maxTouchPoints: 5 });
  const touchedMark = centre((await opened(touching))[0]);
  const tap = async () => {
    await touching.command("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: [touchedMark] });
    await nextFrame();
    await touching.command("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] });
    await nextFrame();
  };
  await tap();
  expect(await until(touching, "(globalThis.gr4Citation || '').startsWith('cite:')"), "a first tap shows the work");
  expect((await touching.evaluate("globalThis.gr4Location || ''")).includes("view=markdown"), "and stays on the slide");
  await tap();
  expect(await until(touching, "(globalThis.gr4Location || '').includes('view=references')"), "a second tap on the mark leads to the references slide");
  await touching.close();
});

console.log(failures.length === 0 ? "qa_BrowserCitations: all checks passed" : `qa_BrowserCitations: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

// Selecting drawn text and copying it, in the browser. SDL3 takes the pointer on the canvas, so the viewer selects
// by itself: a mouse drag that starts on a word selects, Ctrl+C puts the words on the clipboard, a drag that starts
// off text still pans a zoomed slide, a click on a link still follows it, and a finger never selects. The ground
// truth is the demo deck's Markdown, read by hand under spec D's rules (functional_spec_wip.md): words as written, a
// wrapped line joined by a space, a new block or line by a line break. Positions come from the runs the viewer
// publishes, in the units the pointer arrives in, so the test aims at words rather than at pixels of a screenshot.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserSelection.mjs <viewer-web directory>");
  process.exit(2);
}

// The deck no longer has a drawing in steps; the labels its steps reveal are tested on a copy of it with that slide
// added at the end, drawn from figures/stages.svg, which the package still carries
const kStagedSlide = `
# A drawing in steps<br>its Layers advance with the Text {#staged}

:::layout
source: figures/stages.svg
:::

One key advances the prose and the drawing together.

:::step

**Acquire.** The digitiser streams at its native rate.

:::step

**Transform.** Windowing, FFT and averaging run in the GR4 graph.

:::step

**Display.** The spectrum and waterfall are drawn by OpenDigitizer.
`;
const fixture = mkdtempSync(join(tmpdir(), "selection-"));
cpSync(directory, fixture, { recursive: true });
const fixtureDeck = join(fixture, "default", "talk.md");
writeFileSync(fixtureDeck, readFileSync(fixtureDeck, "utf8") + kStagedSlide);

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const frames = async (page, n) => {
  for (let i = 0; i < n; ++i) {
    await page.command("Page.captureScreenshot", { format: "png" });
  }
};

const openAt = async (browser, server, view) => {
  const page = await openPage(browser, `${server.origin}/index.html#view=${view}&step=0`);
  await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
  await page.command("Browser.grantPermissions", { origin: server.origin, permissions: ["clipboardReadWrite", "clipboardSanitizedWrite"] });
  await page.waitFor(`(globalThis.gr4Location || '').includes('view=${view}') && (window.gr4TextRuns || []).length > 0 ? 1 : false`);
  await page.evaluate("document.getElementById('canvas').focus()");
  await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 900, y: 650 });
  await frames(page, 20);
  return page;
};

/// the centre of the first published run whose text starts with `words`, after `after` runs
const runCentre = async (page, words, after = 0) => {
  const runs = JSON.parse(await page.evaluate("JSON.stringify(window.gr4TextRuns)"));
  const index = runs.findIndex((run, at) => at >= after && run[4].trimStart().startsWith(words));
  if (index < 0) {
    throw new Error(`no run starts with ${JSON.stringify(words)}`);
  }
  const [x, y, w, h] = runs[index];
  return { x: x + w / 2, y: y + h / 2, index };
};

const drag = async (page, from, to, extra = {}) => {
  const event = (type, x, y, buttons) => page.command("Input.dispatchMouseEvent", { type, x, y, button: "left", buttons, clickCount: 1, ...extra });
  await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: from.x, y: from.y });
  await frames(page, 2);
  await event("mousePressed", from.x, from.y, 1);
  await frames(page, 2);
  for (let step = 1; step <= 8; ++step) {
    await event("mouseMoved", from.x + ((to.x - from.x) * step) / 8, from.y + ((to.y - from.y) * step) / 8, 1);
    await frames(page, 1);
  }
  await event("mouseReleased", to.x, to.y, 0);
  await frames(page, 2);
};

const copy = async (page) => {
  const key = (type, key, code, vk, modifiers) => page.command("Input.dispatchKeyEvent", { type, key, code, windowsVirtualKeyCode: vk, nativeVirtualKeyCode: vk, modifiers });
  await page.evaluate("navigator.clipboard.writeText('(nothing copied)')");
  await key("rawKeyDown", "Control", "ControlLeft", 17, 2);
  await frames(page, 1);
  await key("rawKeyDown", "c", "KeyC", 67, 2);
  await frames(page, 2);
  await key("keyUp", "c", "KeyC", 67, 2);
  await key("keyUp", "Control", "ControlLeft", 17, 0);
  await frames(page, 3);
  return page.evaluate("navigator.clipboard.readText()");
};

await withViewer(fixture, async ({ server, browser }) => {
  {
    const page = await openAt(browser, server, "markdown");
    // the result column, right of the syntax panel: the panel's lines start with the Markdown, "- unordered ..."
    await drag(page, await runCentre(page, "reduces"), await runCentre(page, "spectral"));
    const line = await copy(page);
    expect(line === "reduces spectral", `a drag along a line copies its words: ${JSON.stringify(line)}`);

    // three list items, each a block of its own, so each is a line of its own
    // list items, each a block of its own, so each is a line of its own; a bullet is decoration and is not copied,
    // an ordered item's number is what the author wrote and is
    const first = await runCentre(page, "unordered");
    await drag(page, first, await runCentre(page, "acquire", first.index));
    const items = await copy(page);
    expect(items === "unordered items, nested:\nwindowing reduces spectral leakage1\nand ordered ones:\n1. acquire", `a drag down a list copies one item per line: ${JSON.stringify(items)}`);

    // a press that does not move is a click: it selects nothing, and clears what was selected
    const word = await runCentre(page, "transform");
    await drag(page, word, word);
    expect((await copy(page)) === "(nothing copied)", "a click on a word selects nothing");

    // a drag that starts off the text pans a zoomed slide as it always has
    await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 640, y: 600 });
    for (let notch = 0; notch < 4; ++notch) {
      await page.command("Input.dispatchMouseEvent", { type: "mouseWheel", x: 640, y: 600, deltaX: 0, deltaY: -120 });
      await frames(page, 2);
    }
    await frames(page, 10);
    const zoomed = JSON.parse(await page.evaluate("JSON.stringify(window.gr4Zoom)"));
    await drag(page, { x: 640, y: 600 }, { x: 540, y: 560 });
    const panned = JSON.parse(await page.evaluate("JSON.stringify(window.gr4Zoom)"));
    expect(zoomed[0] > 1 && (panned[1] !== zoomed[1] || panned[2] !== zoomed[2]), `a drag off the text pans a zoomed slide: ${JSON.stringify(zoomed)} -> ${JSON.stringify(panned)}`);
    await page.close();
  }
  {
    // a code line keeps its spaces: the Python block's "start" line has three before its comment
    const page = await openAt(browser, server, "code");
    const block = await runCentre(page, "@dataclass");
    await drag(page, await runCentre(page, "start", block.index), await runCentre(page, "# Hz", block.index)); // the comment is one token, so one run
    const line2 = await copy(page);
    expect(line2 === "    start: float = 1e3   # Hz", `a drag along a code line copies it with its spaces: ${JSON.stringify(line2)}`);
    await page.close();
  }
  // A click on a link is not asserted here: in headless Chrome a synthesised press reaches SDL3 without a position,
  // so ImGui sees the pointer jump to the corner and takes the click for a drag. A real click follows the link, which
  // the native check under Xvfb, with XTest input, confirms; see functional_task_list.md, task 29.
  {
    // a drawing's labels are text a reader can select, and only once revealed: in stages.svg "acquire" and its note
    // carry data-step="1", "transform" 2 and "display" 3, so step 1 shows two labels and step 3 all six
    const labelsAt = async (step) => {
      const page = await openPage(browser, `${server.origin}/index.html#view=staged&step=${step}`);
      await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
      await page.waitFor(`(globalThis.gr4Location || '').includes('step=${step}') && (window.gr4TextRuns || []).length > 0 ? 1 : false`);
      await frames(page, 20);
      const runs = JSON.parse(await page.evaluate("JSON.stringify(window.gr4TextRuns)"));
      await page.close();
      const labels = ["acquire", "transform", "display", "picoscope, 125 MS/s", "window, FFT, average", "spectrum, waterfall"];
      return labels.filter((label) => runs.some((run) => run[4] === label));
    };
    const first = await labelsAt(1);
    expect(JSON.stringify(first) === JSON.stringify(["acquire", "picoscope, 125 MS/s"]), `at step 1 the drawing offers its first stage's labels: ${JSON.stringify(first)}`);
    const all = await labelsAt(3);
    expect(all.length === 6, `at step 3 it offers all six: ${JSON.stringify(all)}`);
  }
  {
    // a finger never selects: a flick across the words turns the page
    const page = await openAt(browser, server, "markdown");
    await page.command("Emulation.setTouchEmulationEnabled", { enabled: true, maxTouchPoints: 5 });
    await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: true });
    await frames(page, 10);
    // right to left along a line of words, kept off the rim where a browser claims the touch for itself
    const line = await runCentre(page, "reduces");
    const [start, end] = [1280 * 0.85, 1280 * 0.15];
    await page.command("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: [{ x: start, y: line.y }] });
    for (let step = 1; step <= 8; ++step) {
      await page.command("Input.dispatchTouchEvent", { type: "touchMove", touchPoints: [{ x: start + ((end - start) * step) / 8, y: line.y }] });
    }
    await page.command("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] });
    await frames(page, 15);
    expect(!(await page.evaluate("(globalThis.gr4Location || '')")).includes("view=markdown"), `a flick over the words turns the page: ${await page.evaluate("(globalThis.gr4Location || '')")}`);
    expect((await copy(page)) === "(nothing copied)", "and selects nothing");
    await page.close();
  }
});

rmSync(fixture, { recursive: true, force: true });
process.exit(failures.length === 0 ? 0 : 1);

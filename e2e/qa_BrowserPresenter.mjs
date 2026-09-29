// A presenter window is the same deck opened with `?presenter` in a second tab of the same browser. The two keep one
// cursor between them: a key pressed in either moves both. The presenter window keeps its notes up and runs no live
// graph, so the audience's window is the only one that does the work.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt } from "../devtools/png.mjs";
import { cpSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserPresenter.mjs <viewer-web directory>");
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
const kLeft = ["ArrowLeft", "ArrowLeft", 37];

const open = async (browser, url) => {
  const page = await openPage(browser, url);
  await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
  await page.waitFor("document.getElementById('canvas') && (globalThis.gr4Location || '').includes('view=') ? 1 : false");
  await page.evaluate("document.getElementById('canvas').focus()");
  await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 900, y: 360 });
  return page;
};

const hashOf = (page) => page.evaluate("(globalThis.gr4Location || '')");
// a browser stops drawing a tab that is not in front, so each window is brought forward before it is used; two real
// windows side by side are both drawn
const front = async (page) => {
  await page.command("Page.bringToFront");
  await page.evaluate("document.getElementById('canvas').focus()");
};
const follows = (page, wanted) => page.waitFor(`(globalThis.gr4Location || '') === ${JSON.stringify(wanted)} ? 1 : false`);

await withViewer(directory, async ({ server, browser }) => {
  const audience = await open(browser, `${server.origin}/index.html`);
  const presenter = await open(browser, `${server.origin}/index.html?presenter`);

  // the audience leads, two slides on; one back, both windows show the Markdown slide, whose panel spans less than
  // half the width and so cannot be taken for the notes panel's rule
  await front(audience);
  for (let press = 0; press < 2; ++press) {
    const before = await hashOf(audience);
    await audience.press(...kRight);
    await audience.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? 1 : false`);
  }
  const led = await hashOf(audience);
  await front(presenter);
  expect(Boolean(await follows(presenter, led)), `the presenter window follows the audience to ${led}`);

  // and the presenter leads
  await presenter.press(...kLeft);
  await presenter.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(led)} ? 1 : false`);
  const back = await hashOf(presenter);
  await front(audience);
  expect(Boolean(await follows(audience, back)), `the audience window follows the presenter back to ${back}`);

  // the presenter's notes panel covers the foot of its slide under a rule across the whole width; the audience's
  // slide has no such rule
  // a rule is a thin line: a lit row with unlit rows three pixels above and below it, so the inside of a tall panel
  // across the slide is not taken for one
  const widestRule = async (page) => {
    const image = decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
    const litShare = (y) => {
      let lit = 0;
      for (let x = 0; x < image.width; x += 2) {
        const pixel = pixelAt(image, x, y);
        lit += pixel.r + pixel.g + pixel.b > 60 ? 1 : 0;
      }
      return lit / (image.width / 2);
    };
    let widest = 0;
    for (let y = Math.floor(image.height * 0.5); y < image.height - 40; ++y) {
      const share = litShare(y);
      if (share > widest && litShare(y - 3) < 0.5 && litShare(y + 3) < 0.5) {
        widest = share;
      }
    }
    return widest;
  };
  const audienceRule = await widestRule(audience);
  await front(presenter);
  const presenterRule = await widestRule(presenter);
  expect(presenterRule > 0.9 && audienceRule < 0.9, `the presenter window keeps its notes panel up: a rule across ${(presenterRule * 100).toFixed(0)} % of its width, against ${(audienceRule * 100).toFixed(0)} % in the audience's`);

  // on a live slide only the audience's window runs the graph
  const toLive = async (page) => {
    for (let press = 0; press < 120 && !(await hashOf(page)).includes("view=live&"); ++press) {
      const before = await hashOf(page);
      await page.press(...kRight);
      await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? 1 : false`);
    }
  };
  await front(audience);
  await toLive(audience);
  for (let wait = 0; wait < 40; ++wait) {
    await audience.command("Page.captureScreenshot", { format: "png" });
  }
  await front(presenter);
  await follows(presenter, await hashOf(audience));
  for (let wait = 0; wait < 40; ++wait) {
    await presenter.command("Page.captureScreenshot", { format: "png" });
  }
  // the graphs a window has built, with their schedulers' states, as the viewer publishes them
  const graphs = async (page) => Object.values(JSON.parse(await page.evaluate("JSON.stringify(window.gr4Graphs || {})")));
  const inAudience = await graphs(audience);
  const inPresenter = await graphs(presenter);
  expect(inAudience.includes("RUNNING") && inPresenter.length === 0, `only the audience window runs live graphs: ${JSON.stringify(inAudience)} against ${JSON.stringify(inPresenter)}`);

  await presenter.close();
  await audience.close();
});

// The three buttons at the right of the notes: the next step, the next slide past any steps left, and back.
await withViewer(directory, async ({ server, browser }) => {
  const page = await openPage(browser, `${server.origin}/index.html?presenter#view=steps&step=0`);
  await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
  await page.waitFor("document.getElementById('canvas') && window.gr4Zoom && (globalThis.gr4Location || '').includes('view=steps') ? 1 : false");
  const buttons = JSON.parse(await page.waitFor("window.gr4NotesButtons && window.gr4NotesButtons.length === 3 ? JSON.stringify(window.gr4NotesButtons) : false"));
  const [step, skip, back] = buttons;
  const notesHeight = 720 * 0.32; // the panel's share of the window
  expect(step[2] === step[3] && Math.abs(step[2] - notesHeight / 2) <= 1 && step[0] + step[2] > 1280 * 0.95, `the next step is a square half the notes high at their right, ${JSON.stringify(step)} in ${notesHeight} px`);
  expect(skip[1] === back[1] && skip[1] > step[1] + step[3] && skip[0] < back[0] && skip[0] >= step[0] && back[0] + back[2] <= step[0] + step[2] + 1, `with the next slide and back side by side beneath it, ${JSON.stringify(buttons)}`);
  const click = async (index) => {
    const [x, y, w, h] = buttons[index];
    const at = { x: x + w / 2, y: y + h / 2, button: "left", clickCount: 1 };
    const before = await page.evaluate("(globalThis.gr4Location || '')");
    await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: at.x, y: at.y });
    await page.command("Input.dispatchMouseEvent", { type: "mousePressed", ...at });
    await page.command("Input.dispatchMouseEvent", { type: "mouseReleased", ...at });
    await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? 1 : false`);
    return page.evaluate("(globalThis.gr4Location || '')");
  };
  expect((await click(0)) === "#view=steps&step=1", "the top button reveals the next step");
  const skipped = await click(1);
  expect(skipped.includes("step=0") && !skipped.includes("view=steps"), `the middle one goes to the next slide past the steps left, ${skipped}`);
  expect((await click(2)) === "#view=steps&step=1", "and the bottom one goes back to where it was");
  await page.close();
});

// On a phone the presenter view is the remote: the notes take the full width of the top of the screen and the three
// buttons one row beneath them, back, next step and next slide from left to right, none of them over the notes. Long
// notes shrink and then scroll under a dragging finger, and the drag does not turn the page. The ground truth is the
// author's specification (2026-10-02): a shorter side under 600 px, either way up; targets of at least 48 px.
const copy = mkdtempSync(join(tmpdir(), "presenter-phone-"));
try {
  cpSync(directory, copy, { recursive: true });
  const deck = join(copy, "default", "talk.md");
  const text = readFileSync(deck, "utf8");
  const second = text.indexOf("\n# ", text.indexOf("{#intro}"));
  const sentence = "The presenter reads this sentence aloud, and then another one just like it. ";
  // and a slide whose one step reveals a large paragraph, so the miniature of the next press has plainly more ink
  const revealed = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore. ";
  writeFileSync(deck, text.slice(0, second + 1) + `# Long notes {#long-notes}\n\nA slide with a great deal to say.\n\n:::notes\n${sentence.repeat(60)}\n:::\n\n# One big step {#big-step}\n\nA line.\n\n:::step\n\n${revealed.repeat(8)}\n\n# Short notes {#short-notes}\n\nA line.\n\n:::notes\nOne line of notes.\n:::\n\n` + text.slice(second + 1));

  for (const [width, height] of [[390, 844], [844, 390]]) {
    await withViewer(copy, async ({ server, browser }) => {
      const label = `${width}x${height}`;
      const openAt = async (view) => {
        const page = await openPage(browser, "about:blank");
        await page.command("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor: 1, mobile: true });
        await page.command("Page.navigate", { url: `${server.origin}/index.html?presenter#view=${view}` });
        await page.waitFor(`(globalThis.gr4Location || '').includes('view=${view.replace(/&.*/, "")}&') && window.gr4NotesArea && window.gr4NotesArea[2] > 0 && window.gr4NotesButtons.length === 3 ? 1 : false`);
        for (let frame = 0; frame < 10; ++frame) {
          await page.command("Page.captureScreenshot", { format: "png" });
        }
        return page;
      };
      const layoutOf = async (page) => JSON.parse(await page.evaluate("JSON.stringify({ area: window.gr4NotesArea, buttons: window.gr4NotesButtons, miniatures: window.gr4Miniatures || [] })"));

      const page = await openAt("long-notes&step=0");
      const { area, buttons, miniatures } = await layoutOf(page);
      const [x, y, w, h, pixels, , scrollMax] = area;
      const [step, skip, back] = buttons;
      // the slide now shown and the next press's come first: above the notes upright, at their left turned
      const portrait = height > width;
      expect(miniatures.length === 2 && miniatures.every(([mx, my, mw, mh]) => mx >= 0 && my >= 0 && mx + mw <= width && my + mh <= height && Math.abs(mh / mw - 9 / 16) < 0.02), `${label}: two 16:9 miniatures on screen, ${JSON.stringify(miniatures)}`);
      const overlaps = ([ax, ay, aw, ah], [bx, by, bw, bh]) => ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
      expect(miniatures.every((mini) => !overlaps(mini, area) && buttons.every((button) => !overlaps(mini, button))), `${label}: clear of the notes and the buttons`);
      const columnLeft = portrait || miniatures.length < 2 ? 0 : Math.max(...miniatures.map(([mx, , mw]) => mx + mw));
      const column = width - columnLeft;
      expect(w >= column * 0.9 && x >= columnLeft, `${label}: the notes span ${w} of the ${column} px ${portrait ? "under the miniatures" : "beside them"}`);
      const notesBottom = y + h;
      expect(buttons.every(([, by]) => by >= notesBottom), `${label}: every button is beneath the notes, which end at ${notesBottom} px: ${JSON.stringify(buttons)}`);
      expect(buttons.every(([, by, bw, bh]) => by + bh <= height && Math.min(bw, bh) >= 48), `${label}: every button is on screen and at least 48 px`);
      expect(back[0] + back[2] <= step[0] && step[0] + step[2] <= skip[0] && step[2] > skip[2] && skip[2] > back[2], `${label}: back, next step, next slide from left to right, next step the largest`);
      const rowMiddle = (back[0] + skip[0] + skip[2]) / 2;
      expect(Math.abs(rowMiddle - (columnLeft + column / 2)) <= 2, `${label}: the row is centred in its column, its middle at ${rowMiddle.toFixed(0)} px`);



      const hash = await page.evaluate("(globalThis.gr4Location || '')");
      const from = { x: x + w / 2, y: y + h * 0.8 };
      await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", ...from });
      await page.command("Input.dispatchMouseEvent", { type: "mousePressed", ...from, button: "left", clickCount: 1 });
      for (let move = 1; move <= 8; ++move) {
        await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: from.x, y: from.y - move * 15, button: "left", buttons: 1 });
        await page.command("Page.captureScreenshot", { format: "png" });
      }
      await page.command("Input.dispatchMouseEvent", { type: "mouseReleased", x: from.x, y: from.y - 120, button: "left", clickCount: 1 });
      for (let frame = 0; frame < 5; ++frame) {
        await page.command("Page.captureScreenshot", { format: "png" });
      }
      const scrolled = (await layoutOf(page)).area[5];
      expect(scrolled > 40, `${label}: dragging the notes up scrolls them, by ${scrolled} px`);
      const after = await page.evaluate("(globalThis.gr4Location || '')");
      expect(after === hash, `${label}: and does not move the deck, ${hash} -> ${after}`);
      await page.close();

      // the same size on a slide with one line of notes is the size before any shrinking; opened only now, because
      // a second presenter tab shares the cursor and would take the first one with it
      const short = await openAt("short-notes&step=0");
      const unshrunk = (await layoutOf(short)).area[4];
      await short.close();
      expect(pixels < unshrunk && scrollMax > 0, `${label}: long notes shrink, ${pixels} px against ${unshrunk}, and still scroll over ${scrollMax} px`);

      const steps = await openAt("steps&step=0");
      const [sx, sy, sw, sh] = (await layoutOf(steps)).buttons[0];
      const at = { x: sx + sw / 2, y: sy + sh / 2, button: "left", clickCount: 1 };
      await steps.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: at.x, y: at.y });
      await steps.command("Input.dispatchMouseEvent", { type: "mousePressed", ...at });
      await steps.command("Input.dispatchMouseEvent", { type: "mouseReleased", ...at });
      expect(Boolean(await steps.waitFor("(globalThis.gr4Location || '') === '#view=steps&step=1' ? 1 : false")), `${label}: the next-step button reveals the next step`);
      await steps.close();

      // on a slide with steps, the next press reveals a paragraph: the next miniature carries more ink than the current
      {
        const stepped = await openAt("big-step&step=0");
        for (let frame = 0; frame < 30; ++frame) {
          await stepped.command("Page.captureScreenshot", { format: "png" });
        }
        const image = decodePng(Buffer.from((await stepped.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
        const shown = (await layoutOf(stepped)).miniatures;
        const inkIn = ([mx, my, mw, mh]) => {
          const scale = image.width / width;
          const backdrop = pixelAt(image, Math.floor((mx + 3) * scale), Math.floor((my + mh / 2) * scale));
          let ink = 0;
          for (let py = Math.floor(my * scale) + 2; py < (my + mh) * scale - 2; ++py) {
            for (let px = Math.floor(mx * scale) + 2; px < (mx + mw) * scale - 2; ++px) {
              const pixel = pixelAt(image, px, py);
              ink += Math.abs(pixel.r - backdrop.r) + Math.abs(pixel.g - backdrop.g) + Math.abs(pixel.b - backdrop.b) > 60 ? 1 : 0;
            }
          }
          return ink;
        };
        const [now, next] = shown.length === 2 ? shown.map(inkIn) : [0, 0];
        expect(now > 0 && next > now * 1.2, `${label}: the miniatures show the slide, and the next one the step it reveals: ink ${now} now, ${next} next`);
        await stepped.close();
      }
    });
  }
} finally {
  rmSync(copy, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserPresenter: all checks passed" : `qa_BrowserPresenter: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

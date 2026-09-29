// Keys a presentation remote sends move the cursor, which is mirrored into the address bar for deep links and reloads.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { readFileSync } from "node:fs";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserNavigation.mjs <viewer-web directory>");
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

await withViewer(directory, async ({ server, browser }) => {
  const page = await openPage(browser, `${server.origin}/index.html`);
  // the launch overlay goes when the runtime starts, before the presentation has loaded; the cursor says it is ready
  expect(
    await page.waitFor("(globalThis.gr4Location || '').includes('view=') && (globalThis.gr4Location || '').includes('step=0')"),
    "the viewer publishes its starting cursor once the presentation is loaded",
  );
  await page.evaluate("document.getElementById('canvas').focus()");

  const start = await page.evaluate("(globalThis.gr4Location || '')");

  await page.press(...kRight);
  const advanced = await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(start)} ? (globalThis.gr4Location || '') : false`);
  expect(Boolean(advanced), `a forward key moves the cursor, from ${JSON.stringify(start)} to ${JSON.stringify(advanced)}`);
  expect(String(advanced).includes("view=") && String(advanced).includes("step="), "the published cursor names both the view and the step");
  // a browser's mouse enter after a key carries the position (0, 0), inside the menu's reveal margin; no pointer came near it
  for (let frame = 0; frame < 30; ++frame) {
    await page.command("Page.captureScreenshot", { format: "png" });
  }
  const menu = await page.evaluate("JSON.stringify(window.gr4MenuEntries || [])");
  expect(menu === "[]", `a remote's key leaves the side menu closed, it shows ${menu.length > 2 ? "its entries" : "nothing"}`);

  await page.press(...kLeft);
  const retreated = await page.waitFor(`(globalThis.gr4Location || '') === ${JSON.stringify(start)} ? (globalThis.gr4Location || '') : false`);
  expect(Boolean(retreated), `a backward key returns where it came from, got ${JSON.stringify(retreated)}`);

  if (failures.length > 0) {
    console.error("console output:\n  " + page.consoleLines.join("\n  "));
  }
  await page.close();

  // Ground truth: the deck's own headings in default/talk.md, counted from 1, give the footer numbers
  const headings = readFileSync(join(directory, "default", "talk.md"), "utf8")
    .split("\n")
    .filter((line) => /^# /.test(line) && /\{#[^}]+\}\s*$/.test(line));
  const number = headings.findIndex((line) => line.includes("{#fonts}")) + 1;
  expect(number > 0, `the deck has a slide with id 'fonts', number ${number}`);
  const byId = await openPage(browser, `${server.origin}/index.html#view=fonts&step=0`);
  expect(await byId.waitFor(`(globalThis.gr4Location || '').includes('view=fonts&') ? 1 : false`), "an id in the address still opens its slide");
  expect(
    await byId.waitFor(`location.hash === '#view=${number}&step=0' ? 1 : false`),
    `and the address then shows its number: ${await byId.evaluate("location.hash")}, wanted #view=${number}&step=0`,
  );
  await byId.close();
  const byNumber = await openPage(browser, `${server.origin}/index.html#view=${number}&step=0`);
  expect(
    await byNumber.waitFor(`(globalThis.gr4Location || '').includes('view=fonts&') ? 1 : false`),
    `#view=${number} opens the slide with that footer number`,
  );
  await byNumber.close();

  // the viewer's own address writes must not be taken for typed ones, or a quick key would be undone by its echo
  const typed = await openPage(browser, `${server.origin}/index.html#view=intro&step=0`);
  await typed.waitFor("(globalThis.gr4Location || '').includes('view=intro&') ? 1 : false");
  await typed.evaluate(`location.hash = '#view=${number}&step=0'`);
  expect(
    await typed.waitFor(`(globalThis.gr4Location || '').includes('view=fonts&') ? 1 : false`),
    `a typed #view=${number} turns the open page to that slide, at ${await typed.evaluate("globalThis.gr4Location")}`,
  );
  await typed.evaluate("location.hash = '#view=intro&step=0'");
  expect(
    await typed.waitFor("(globalThis.gr4Location || '').includes('view=intro&') ? 1 : false"),
    `and a typed id back to it, at ${await typed.evaluate("globalThis.gr4Location")}`,
  );
  await typed.evaluate("document.getElementById('canvas').focus()");
  await typed.press(...kRight);
  const moved = await typed.waitFor("!(globalThis.gr4Location || '').includes('view=intro&step=0') ? globalThis.gr4Location : false");
  for (let frame = 0; frame < 30; ++frame) {
    await typed.command("Page.captureScreenshot", { format: "png" });
  }
  expect(
    moved && (await typed.evaluate("globalThis.gr4Location")) === moved,
    `a key press stays where it went, not undone by the address it wrote: ${moved} then ${await typed.evaluate("globalThis.gr4Location")}`,
  );
  await typed.close();
});

process.exit(failures.length === 0 ? 0 : 1);

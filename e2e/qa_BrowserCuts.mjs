// Only a step forward moves: a step back and a jump cut to where they land, a jump lands on its slide's last step, Home on
// the first slide's first step, and a slide entered backwards does not go on by itself until the next step forward.
// Ground truth: `gr4Location` (the cursor) and `gr4Moving` (a move between views is running), sampled every frame.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { cpSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserCuts.mjs <viewer-web directory>");
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
const kAdvanceSeconds = 1.5;

// two pushes of two seconds each, so a running move is seen in many frames; `two` goes on by itself
const deck = `---
title: cuts
---

# One {#one}

The first slide.

:::step
Its second step.

# Two {#two}

:::layout
transition: push
duration: 2
advance: ${kAdvanceSeconds}s
:::

The second slide.

:::step
Its second step.

# Three {#three}

:::layout
transition: push
duration: 2
:::

The third slide.
`;

const copy = mkdtempSync(join(tmpdir(), "cuts-"));
try {
  cpSync(directory, copy, { recursive: true });
  writeFileSync(join(copy, "default", "talk.md"), deck);

  await withViewer(copy, async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html#view=one&step=1`);
    const at = (view, step) => `(globalThis.gr4Location || '').includes('view=${view}&step=${step}') ? 1 : false`;
    const location = () => page.evaluate("globalThis.gr4Location || ''");
    // frames for `milliseconds`: whether any of them moved
    const movedWithin = async (milliseconds) => {
      const started = Date.now();
      let moved = false;
      while (Date.now() - started < milliseconds) {
        await page.command("Page.captureScreenshot", { format: "png" });
        moved = moved || (await page.evaluate("globalThis.gr4Moving === true"));
      }
      return moved;
    };
    expect(await page.waitFor(at("one", 1)), "the deck opens on its first slide's last step");
    await page.evaluate("document.getElementById('canvas').focus()");

    await page.press(...kRight);
    expect(await movedWithin(1000), "a step forward onto another slide moves");
    expect(await page.waitFor(at("two", 1), 15000), `and the slide it lands on goes on by itself to its next step, at ${await location()}`);
    expect(await page.waitFor(at("three", 0), 15000), `then to the next slide, at ${await location()}`);
    await movedWithin(2500); // the move into `three` ends

    await page.press(...kLeft);
    expect(await page.waitFor(at("two", 1)), `a step back lands on the last step of the slide before, at ${await location()}`);
    expect(!(await movedWithin(1000)), "and cuts to it");
    await movedWithin((kAdvanceSeconds + 1.5) * 1000);
    expect((await location()).includes("view=two&step=1"), `a slide entered backwards does not go on by itself, at ${await location()}`);

    await page.press(...kLeft);
    expect(await page.waitFor(at("two", 0)), `a step back within a slide, at ${await location()}`);
    await movedWithin((kAdvanceSeconds + 1.5) * 1000);
    expect((await location()).includes("view=two&step=0"), `does not start it going on by itself either, at ${await location()}`);
    await page.press(...kRight);
    expect(await page.waitFor(at("two", 1)), "a step forward does");
    expect(await page.waitFor(at("three", 0), 15000), `and the slide goes on by itself again, at ${await location()}`);
    await movedWithin(2500);

    await page.evaluate("location.hash = '#view=two'");
    expect(await page.waitFor(at("two", 1)), `a typed address without a step lands on the slide's last step, at ${await location()}`);
    expect(!(await movedWithin(1000)), "and cuts to it");
    await movedWithin((kAdvanceSeconds + 1.5) * 1000);
    expect((await location()).includes("view=two&step=1"), `a jump does not start the slide going on by itself, at ${await location()}`);

    await page.press("Home", "Home", 36);
    expect(await page.waitFor(at("one", 0)), `Home lands on the first slide's first step, at ${await location()}`);
    expect(!(await movedWithin(1000)), "and cuts to it");

    if (failures.length > 0) {
      console.error("console output:\n  " + page.consoleLines.join("\n  "));
    }
    await page.close();
  });
} finally {
  rmSync(copy, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserCuts: all checks passed" : `qa_BrowserCuts: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

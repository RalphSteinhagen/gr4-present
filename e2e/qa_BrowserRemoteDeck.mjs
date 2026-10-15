// A deck the viewer does not bundle, served from another origin, loads, draws its figure and steps through both slides.

import { openPage, startServer, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt } from "../devtools/png.mjs";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserRemoteDeck.mjs <viewer-web directory>");
  process.exit(2);
}
const deckDirectory = join(import.meta.dirname, "..", "test", "fixtures", "remote-deck"); // never staged with the viewer

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const kRight = ["ArrowRight", "ArrowRight", 39];
const kFigureTeal = { r: 0, g: 128, b: 128 }; // test/fixtures/remote-deck/figures/square.svg
const kTealTolerance = 12;
const kFigureShownPixels = 1000;

const tealPixelsOn = async (page) => {
  const shot = decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
  let teal = 0;
  for (let y = 0; y < shot.height; y += 2) {
    for (let x = 0; x < shot.width; x += 2) {
      const pixel = pixelAt(shot, x, y);
      const isTeal =
        Math.abs(pixel.r - kFigureTeal.r) <= kTealTolerance &&
        Math.abs(pixel.g - kFigureTeal.g) <= kTealTolerance &&
        Math.abs(pixel.b - kFigureTeal.b) <= kTealTolerance;
      teal += isTeal ? 4 : 0;
    }
  }
  return teal;
};

const deck = await startServer(deckDirectory, ["--cors"]);
try {
  await withViewer(directory, async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html?presentation=${encodeURIComponent(deck.origin)}`);
    expect(await page.waitFor("(globalThis.gr4Location || '').includes('view=remote&step=0')"), `the deck from ${deck.origin} opens at its first view`);
    await page.evaluate("document.getElementById('canvas').focus()");

    let teal = 0;
    for (let attempt = 0; attempt < 40 && teal < kFigureShownPixels; ++attempt) {
      teal = await tealPixelsOn(page);
    }
    expect(teal >= kFigureShownPixels, `its figure, fetched from the same origin as the deck, is drawn: ${teal} teal pixels`);

    await page.press(...kRight);
    expect(await page.waitFor("(globalThis.gr4Location || '').includes('view=remote&step=1')"), "the step reveals itself");
    await page.press(...kRight);
    expect(await page.waitFor("(globalThis.gr4Location || '').includes('view=second&step=0')"), "and the next press reaches the second slide");

    // the presenter's window opens on the same deck: its address keeps `presentation`
    const targetsBefore = await (await fetch(`${browser.endpoint}/json/list`)).json();
    await page.press("s", "KeyS", 83);
    let presenterTarget = null;
    for (let attempt = 0; attempt < 100 && !presenterTarget; ++attempt) {
      const targets = await (await fetch(`${browser.endpoint}/json/list`)).json();
      presenterTarget = targets.find((target) => target.url.includes("presenter") && !targetsBefore.some((before) => before.id === target.id));
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    expect(!!presenterTarget && presenterTarget.url.includes("presentation="), `S opens the presenter's window on this deck (${presenterTarget?.url ?? "none"})`);
    if (presenterTarget) {
      const presenter = await openPage(browser, presenterTarget.url);
      expect(await presenter.waitFor("(globalThis.gr4Location || '').includes('view=second')", 60_000), "and it shows the deck's slide, not the built-in deck's");
      await presenter.close();
    }

    if (failures.length > 0) {
      console.error("console output:\n  " + page.consoleLines.join("\n  "));
    }
    await page.close();
  });
} finally {
  deck.stop();
}

process.exit(failures.length === 0 ? 0 : 1);

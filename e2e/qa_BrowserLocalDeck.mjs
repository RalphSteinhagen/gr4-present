// Load and Save in a browser without Chromium's file handles (as in Firefox and Safari), with no server for the deck: a
// folder through the chooser a real click on Load opens, a lone Markdown file and a .tar.gz dropped onto the window;
// Save as a .tar.gz download; and a path in the address, which only a kept handle opens, refused.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt } from "../devtools/png.mjs";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserLocalDeck.mjs <viewer-web directory>");
  process.exit(2);
}
const deckDirectory = join(import.meta.dirname, "..", "test", "fixtures", "remote-deck"); // never staged with the viewer
const scratch = mkdtempSync(join(tmpdir(), "gr4-present-local-deck-"));

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const kControl = 2; // the DevTools protocol's modifier bit for Ctrl
const at = (entry) => ({ x: entry[2] + entry[4] / 2, y: entry[3] + entry[5] / 2 });

// ImGui takes a press on what the pointer hovered in an earlier frame, so the pointer arrives first
const nextFrame = () => new Promise((resolve) => setTimeout(resolve, 100));
const click = async (page, point) => {
  await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: point.x, y: point.y });
  await nextFrame();
  for (const type of ["mousePressed", "mouseReleased"]) {
    await page.command("Input.dispatchMouseEvent", { type, x: point.x, y: point.y, button: "left", clickCount: 1 });
    await nextFrame();
  }
};

// the side menu slides out as the pointer nears the left edge; Load and Save publish where they are drawn
const fileEntry = async (page, kind, index) => {
  const probe = `(() => { const entry = (window.gr4FileEntries || []).find((e) => e[0] === "${kind}" && e[1] === ${index}); return entry && entry[2] >= 0 ? JSON.stringify(entry) : ""; })()`;
  for (let attempt = 0; attempt < 100; ++attempt) {
    await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 4 + (attempt % 2), y: 300 });
    const found = await page.evaluate(probe);
    if (found) {
      return JSON.parse(found);
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`the side menu never showed ${kind} ${index}`);
};

// a real click on Load must open the browser's folder chooser: the chooser needs the click's user activation, though
// the viewer acts on it a frame later
const chooseThroughLoad = async (page, files) => {
  const load = at(await fileEntry(page, "file", 0));
  const seen = page.eventCount();
  await click(page, load);
  const opened = await page.waitForEvent("Page.fileChooserOpened", seen, 20_000);
  await page.command("DOM.setFileInputFiles", { files, backendNodeId: opened.params.backendNodeId });
  return opened;
};

// as a person types it: Control goes down first, which is where SDL takes the modifier from
const pressControl = async (page, key, code, keyCode) => {
  await page.command("Input.dispatchKeyEvent", { type: "rawKeyDown", key: "Control", code: "ControlLeft", windowsVirtualKeyCode: 17, modifiers: kControl });
  for (const type of ["rawKeyDown", "keyUp"]) {
    await page.command("Input.dispatchKeyEvent", { type, key, code, windowsVirtualKeyCode: keyCode, modifiers: kControl });
  }
  await page.command("Input.dispatchKeyEvent", { type: "keyUp", key: "Control", code: "ControlLeft", windowsVirtualKeyCode: 17 });
};

const locationOf = (page) => page.evaluate("globalThis.gr4Location || ''");

/// `file` dropped onto the window, as a file dragged from the desktop arrives
const drop = (page, file) =>
  page.evaluate(`(() => {
    const transfer = new DataTransfer();
    transfer.items.add(new File([Uint8Array.from(atob("${readFileSync(file).toString("base64")}"), (c) => c.charCodeAt(0))], ${JSON.stringify(file.split("/").at(-1))}));
    const canvas = document.getElementById("canvas");
    canvas.dispatchEvent(new DragEvent("drop", { dataTransfer: transfer, bubbles: true, cancelable: true, clientX: 200, clientY: 200 }));
    return true;
  })()`);

/// whether the page logs a line holding `text`, from its `from`-th line on, within `timeoutMs`
const logs = async (page, text, from = 0, timeoutMs = 10_000) => {
  for (const deadline = Date.now() + timeoutMs; Date.now() < deadline; await new Promise((resolve) => setTimeout(resolve, 100))) {
    if (page.consoleLines.slice(from).some((line) => line.includes(text))) {
      return true;
    }
  }
  return false;
};

const kFigureTeal = { r: 0, g: 128, b: 128 }; // test/fixtures/remote-deck/figures/square.svg
const kTealTolerance = 12;
const tealPixelsOn = async (page) => {
  const shot = decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
  let teal = 0;
  for (let y = 0; y < shot.height; y += 2) {
    for (let x = 0; x < shot.width; x += 2) {
      const pixel = pixelAt(shot, x, y);
      const isTeal = ["r", "g", "b"].every((channel) => Math.abs(pixel[channel] - kFigureTeal[channel]) <= kTealTolerance);
      teal += isTeal ? 4 : 0;
    }
  }
  return teal;
};

const loneMarkdown = join(scratch, "lone.md");
writeFileSync(loneMarkdown, "# Lone slide {#lone}\n\nno manifest, no figures\n");
const archive = join(scratch, "remote-deck.tar.gz");
execFileSync("tar", ["-czf", archive, "-C", join(deckDirectory, ".."), "remote-deck"]);
const downloads = join(scratch, "downloads");
const notADeck = join(scratch, "not-a-deck");
execFileSync("mkdir", ["-p", notADeck]);
writeFileSync(join(notADeck, "notes.txt"), "no index.yml here\n");
const markdownFolder = join(scratch, "markdown-only");
execFileSync("mkdir", ["-p", join(markdownFolder, "older")]);
writeFileSync(join(markdownFolder, "talk.md"), "# In a folder {#infolder}\n\nno index.yml beside it\n");
writeFileSync(join(markdownFolder, "older", "draft.md"), "# Older draft {#draft}\n");

try {
  await withViewer(directory, async ({ server, browser }) => {
    // the pickers Firefox and Safari have: Chromium's file handles are qa_BrowserLiveDeck's
    const page = await openPage(browser, "about:blank");
    await page.command("Page.enable");
    await page.command("Page.addScriptToEvaluateOnNewDocument", { source: "for (const name of ['showDirectoryPicker', 'showOpenFilePicker']) Object.defineProperty(globalThis, name, { value: undefined });" });
    await page.command("Page.navigate", { url: `${server.origin}/index.html` });
    await page.command("Page.setInterceptFileChooserDialog", { enabled: true });
    expect(await page.waitFor("(globalThis.gr4Location || '').includes('step=')", 120_000), "the built-in deck opens");
    const byPath = await openPage(browser, "about:blank");
    await byPath.command("Page.enable");
    await byPath.command("Page.addScriptToEvaluateOnNewDocument", { source: "for (const name of ['showDirectoryPicker', 'showOpenFilePicker']) Object.defineProperty(globalThis, name, { value: undefined });" });
    await byPath.command("Page.navigate", { url: `${server.origin}/index.html?presentation=${encodeURIComponent("file:/home/me/talks/tour")}` });
    expect(await logs(byPath, "cannot reopen file:/home/me/talks/tour", 0, 60_000).catch(() => false), "a path in the address, which only a kept handle opens, is refused with a pointer to Load");
    await byPath.close();
    await page.evaluate("document.getElementById('canvas').focus()");

    const folder = await chooseThroughLoad(page, [deckDirectory]);
    expect(folder.params.mode === "uploadFolder" || folder.params.mode === "selectSingle", `Load → a deck's folder opens a folder chooser (${folder.params.mode})`);
    expect(await page.waitFor("(globalThis.gr4Location || '').includes('view=remote&step=0')", 60_000), "the folder's deck opens at its first view, with no server for it");
    let teal = 0;
    for (let attempt = 0; attempt < 40 && teal < 1000; ++attempt) {
      teal = await tealPixelsOn(page);
    }
    expect(teal >= 1000, `its figure, read from the folder in memory, is drawn: ${teal} teal pixels`);

    const refusedFrom = page.consoleLines.length;
    await chooseThroughLoad(page, [notADeck]);
    expect(await logs(page, "not-a-deck", refusedFrom), "a folder with neither index.yml nor a .md is refused, named in Diagnostics");
    expect((await locationOf(page)).includes("view=remote"), "and the deck shown stays");
    expect(await page.evaluate("FS.analyzePath('/local/remote-deck/index.yml').exists"), "with its files in memory");

    await page.command("Browser.setDownloadBehavior", { behavior: "allow", downloadPath: downloads });
    await page.evaluate(
      "globalThis.gr4SaveKeyPrevented = null; window.addEventListener('keydown', (event) => { if (event.ctrlKey && event.key === 's') globalThis.gr4SaveKeyPrevented = event.defaultPrevented; })",
    );
    await pressControl(page, "s", "KeyS", 83);
    const saved = join(downloads, "remote-deck.tar.gz");
    const deadline = Date.now() + 30_000;
    while (!existsSync(saved) && Date.now() < deadline) {
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    expect(existsSync(saved), `Ctrl+S downloads remote-deck.tar.gz (in ${existsSync(downloads) ? readdirSync(downloads).join(", ") || "nothing" : "nothing"})`);
    if (existsSync(saved)) {
      const listed = execFileSync("tar", ["-tzf", saved], { encoding: "utf8" }).trim().split("\n").sort();
      expect(
        JSON.stringify(listed) === JSON.stringify(["remote-deck/figures/square.svg", "remote-deck/index.yml", "remote-deck/talk.md"]),
        `GNU tar lists the deck's three files under remote-deck/: ${listed.join(", ")}`,
      );
    }
    expect((await page.evaluate("globalThis.gr4SaveKeyPrevented")) === true, "the browser's own Save does not answer Ctrl+S");

    const reached = (fragment, timeoutMs) => page.waitFor(`(globalThis.gr4Location || '').includes('${fragment}')`, timeoutMs).catch(() => false);
    const manifestFrom = page.consoleLines.length;
    await drop(page, join(deckDirectory, "index.yml"));
    expect(await logs(page, "the folder holding it", manifestFrom), "a deck's index.yml dropped alone is refused, pointing at its folder");
    expect((await locationOf(page)).includes("view=remote"), "and the deck shown stays");

    await drop(page, loneMarkdown);
    expect(await reached("view=lone&", 60_000), `a dropped .md opens as a deck of its own (${await locationOf(page)})`);
    expect(await page.evaluate("!FS.analyzePath('/tmp/filedrop/0').exists"), "and SDL's copy of it is removed");

    await chooseThroughLoad(page, [markdownFolder]);
    expect(await reached("view=infolder&", 60_000), `Load → a folder without index.yml opens its least nested .md (${await locationOf(page)})`);

    await drop(page, archive);
    expect(await reached("view=remote&step=0", 60_000), `a dropped .tar.gz opens the deck inside (${await locationOf(page)})`);
    expect(await page.evaluate("FS.analyzePath('/local/deck/remote-deck/index.yml').exists"), "from the archive's folder in memory");

    const saveEntry = await fileEntry(page, "file", 1); // the row does not depend on the deck

    if (failures.length > 0) {
      console.error("console output:\n  " + page.consoleLines.slice(-30).join("\n  "));
    }
    await page.close();

    // Save on the failure screen, which publishes nothing of its menu: reported, and the page goes on
    const failing = await openPage(browser, `${server.origin}/index.html?presentation=${encodeURIComponent("http://localhost:1")}`);
    expect(await logs(failing, "no presentation here", 0, 60_000), "a deck that cannot be read shows the failure screen");
    await failing.command("Page.bringToFront");
    await failing.evaluate("document.getElementById('canvas').focus()");
    for (let attempt = 0; attempt < 20; ++attempt) {
      await failing.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 4 + (attempt % 2), y: 300 });
      await nextFrame();
    }
    await click(failing, at(saveEntry));
    expect(await logs(failing, "deck could not be saved"), "Save on the failure screen reports that there is nothing to save");
    const afterClick = failing.consoleLines.length;
    await pressControl(failing, "s", "KeyS", 83);
    expect(await logs(failing, "deck could not be saved", afterClick), "and the viewer goes on: Ctrl+S then reports it again");
    await failing.close();
  });
} finally {
  execFileSync("rm", ["-rf", scratch]);
}

process.exit(failures.length === 0 ? 0 : 1);

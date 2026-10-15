// A deck from this computer through the file handles Chromium keeps: Load reads a folder's files, "follow edits" reloads
// the deck when one changes, F5 reopens them as they are now (after a click on "Reopen" if the browser asks again), the
// presenter's window reopens them itself, and a dropped .md or folder is kept as a picked one is. Headless Chrome cannot
// answer the real picker, nor drop a desktop's file, so they hand out handles of the origin's private file system
// (OPFS), which are the same kind of handle and change the same way.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { readFileSync } from "node:fs";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserLiveDeck.mjs <viewer-web directory>");
  process.exit(2);
}
const deckDirectory = join(import.meta.dirname, "..", "test", "fixtures", "remote-deck");

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

// as strict as the real picker: it opens only while a click or key press is fresh; a dropped item's handle is the
// file or folder of its name under OPFS's dropped/
const kPickers = `
  globalThis.showDirectoryPicker = async () => {
    if (!navigator.userActivation.isActive) throw new DOMException("Must be handling a user gesture to show a file picker.", "SecurityError");
    let folder = await navigator.storage.getDirectory();
    for (const part of (globalThis.gr4PickerAnswer || "live-deck").split("/")) folder = await folder.getDirectoryHandle(part);
    return folder;
  };
  DataTransferItem.prototype.getAsFileSystemHandle = function () {
    const name = this.getAsFile().name;
    return navigator.storage.getDirectory().then((root) => root.getDirectoryHandle("dropped")).then((dropped) => dropped.getDirectoryHandle(name).catch(() => dropped.getFileHandle(name)));
  };
`;
// the browser asks again after F5, as Chromium does unless "allow on every visit" was chosen
const kAsksAgain = `
  FileSystemHandle.prototype.queryPermission = async () => "prompt";
  FileSystemHandle.prototype.requestPermission = async () => "granted";
`;

const nextFrame = () => new Promise((resolve) => setTimeout(resolve, 100));
const click = async (page, point) => {
  await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: point.x, y: point.y });
  await nextFrame();
  for (const type of ["mousePressed", "mouseReleased"]) {
    await page.command("Input.dispatchMouseEvent", { type, x: point.x, y: point.y, button: "left", clickCount: 1 });
    await nextFrame();
  }
};
const fileEntry = async (page, kind, index) => {
  const probe = `(() => { const entry = (window.gr4FileEntries || []).find((e) => e[0] === "${kind}" && e[1] === ${index}); return entry && entry[2] >= 0 ? JSON.stringify(entry) : ""; })()`;
  for (let attempt = 0; attempt < 100; ++attempt) {
    await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 4 + (attempt % 2), y: 300 });
    const found = await page.evaluate(probe);
    if (found) {
      const entry = JSON.parse(found);
      return { x: entry[2] + entry[4] / 2, y: entry[3] + entry[5] / 2 };
    }
    await nextFrame();
  }
  throw new Error(`the side menu never showed ${kind} ${index}`);
};
const reached = (page, fragment, timeoutMs = 60_000) => page.waitFor(`(globalThis.gr4Location || '').includes('${fragment}')`, timeoutMs).catch(() => false);
/// `text` written to `path` under OPFS's root, as an editor saves it
const writeFile = (page, path, text) =>
  page.evaluate(`(async () => {
    let folder = await navigator.storage.getDirectory();
    const parts = ${JSON.stringify(path)}.split("/");
    for (const part of parts.slice(0, -1)) folder = await folder.getDirectoryHandle(part, { create: true });
    const writable = await (await folder.getFileHandle(parts.at(-1), { create: true })).createWritable();
    await writable.write(${JSON.stringify(text)});
    await writable.close();
    return true;
  })()`);

/// the centre of the page's "Open …" or "Reopen …" button once it says `text`
const buttonSaying = (page, text) =>
  page
    .waitFor(`(() => { const b = document.getElementById("gr4-reopen"); if (!b || !b.textContent.includes(${JSON.stringify(text)})) return ""; const r = b.getBoundingClientRect(); return JSON.stringify({ x: r.x + r.width / 2, y: r.y + r.height / 2 }); })()`, 60_000)
    .then(JSON.parse)
    .catch(() => null);

/// a file or folder of that name dropped onto the window
const drop = (page, name) =>
  page.evaluate(`(() => {
    const transfer = new DataTransfer();
    transfer.items.add(new File([], ${JSON.stringify(name)}));
    document.getElementById("canvas").dispatchEvent(new DragEvent("drop", { dataTransfer: transfer, bubbles: true, cancelable: true, clientX: 200, clientY: 200 }));
    return true;
  })()`);

/// a page whose scripts `before` runs ahead of the viewer's own, then `url`
const openWith = async (browser, url, before) => {
  const page = await openPage(browser, "about:blank");
  await page.command("Page.enable");
  for (const source of before) {
    await page.command("Page.addScriptToEvaluateOnNewDocument", { source });
  }
  await page.command("Page.navigate", { url });
  return page;
};

await withViewer(directory, async ({ server, browser }) => {
  const page = await openWith(browser, `${server.origin}/index.html`, [kPickers]);
  expect(await reached(page, "step=", 120_000), "the built-in deck opens");
  await page.evaluate("document.getElementById('canvas').focus()");
  await writeFile(page, "live-deck/index.yml", readFileSync(join(deckDirectory, "index.yml"), "utf8"));
  await writeFile(page, "live-deck/talk.md", "# Live {#live}\n\nas first written\n");
  await writeFile(page, "live-deck/figures/square.svg", readFileSync(join(deckDirectory, "figures", "square.svg"), "utf8"));

  await click(page, await fileEntry(page, "file", 0));
  expect(await reached(page, "view=live"), "Load → a deck's folder reads it through its handle");
  expect((await page.evaluate("location.search")).includes("presentation=local%3Alive-deck"), "the address names it: ?presentation=local:live-deck");

  // Chromium's folder chooser on Linux picks the subfolder highlighted in it, inside a deck's folder its first
  const refusedFrom = page.consoleLines.length;
  await page.evaluate("globalThis.gr4PickerAnswer = 'live-deck/figures'");
  await click(page, await fileEntry(page, "file", 0));
  let hinted = false;
  for (const deadline = Date.now() + 15_000; !hinted && Date.now() < deadline; await nextFrame()) {
    hinted = page.consoleLines.slice(refusedFrom).some((line) => line.includes("figures") && line.includes("subfolders highlighted"));
  }
  expect(hinted, "a deck's subfolder picked instead of the deck's folder is refused, the likely cause named");
  expect((await page.evaluate("location.search")).includes("presentation=local%3Alive-deck"), "and the address still names the deck shown");
  expect((await page.evaluate("globalThis.gr4Location || ''")).includes("view=live"), "which stays");
  await page.evaluate("globalThis.gr4PickerAnswer = ''");

  await writeFile(page, "live-deck/talk.md", "# Not followed {#unfollowed}\n\nnot shown\n");
  await new Promise((resolve) => setTimeout(resolve, 5000));
  expect((await page.evaluate("globalThis.gr4Location || ''")).includes("view=live"), "an edit while 'follow edits' is off is not loaded");

  await click(page, await fileEntry(page, "follow", 0));
  await writeFile(page, "live-deck/talk.md", "# Edited {#edited}\n\nsaved while followed\n");
  expect(await reached(page, "view=edited", 15_000), "with 'follow edits' ticked, the saved edit reloads the deck");

  await page.command("Page.reload");
  expect(await reached(page, "view=edited", 60_000), "F5 reopens the files as they are now");

  await page.command("Page.addScriptToEvaluateOnNewDocument", { source: kAsksAgain });
  await page.command("Page.reload");
  const button = await page.waitFor("(() => { const b = document.getElementById('gr4-reopen'); if (!b) return ''; const r = b.getBoundingClientRect(); return JSON.stringify([r.x + r.width / 2, r.y + r.height / 2]); })()", 60_000).catch(() => "");
  expect(!!button, "a browser that asks again shows 'Reopen live-deck'");
  if (button) {
    const [x, y] = JSON.parse(button);
    await click(page, { x, y });
    expect(await reached(page, "view=edited", 60_000), "and a click on it reopens the deck");
  }

  await page.evaluate("document.getElementById('canvas').focus()");
  const targetsBefore = await (await fetch(`${browser.endpoint}/json/list`)).json();
  // a fresh page's first key asks for full screen and spends the key's activation; the second opens the window
  await page.press("s", "KeyS", 83);
  await new Promise((resolve) => setTimeout(resolve, 500));
  await page.press("s", "KeyS", 83);
  let presenterTarget = null;
  for (let attempt = 0; attempt < 100 && !presenterTarget; ++attempt) {
    const targets = await (await fetch(`${browser.endpoint}/json/list`)).json();
    presenterTarget = targets.find((target) => target.url.includes("presenter") && !targetsBefore.some((before) => before.id === target.id));
    await nextFrame();
  }
  const presenterUrl = presenterTarget?.url ?? "none";
  expect(presenterUrl.includes("presentation=local%3Alive-deck") && !presenterUrl.includes("handover"), `S opens the presenter's window on the kept deck, no hand-over (${presenterUrl})`);
  if (presenterTarget) {
    const presenter = await openPage(browser, presenterTarget.url);
    expect(await reached(presenter, "view=edited", 60_000), "the presenter's window reopens the handles itself");
    await presenter.close();
  }

  await writeFile(page, "dropped/notes.md", "# Dropped {#dropped}\n\nas first written\n");
  await drop(page, "notes.md");
  expect(await reached(page, "view=dropped", 60_000), "a dropped .md opens through its handle");
  expect((await page.evaluate("location.search")).includes("presentation=local%3Anotes.md"), "the address names it: ?presentation=local:notes.md");
  await click(page, await fileEntry(page, "follow", 0)); // F5 began a page that does not follow yet
  await writeFile(page, "dropped/notes.md", "# Dropped and edited {#droppededited}\n\nsaved while followed\n");
  expect(await reached(page, "view=droppededited", 15_000), "with 'follow edits' ticked, an edit of the dropped .md reloads it");

  await writeFile(page, "dropped/only-markdown/talk.md", "# Only Markdown {#onlymarkdown}\n");
  await drop(page, "only-markdown");
  expect(await reached(page, "view=onlymarkdown", 60_000), "a dropped folder without index.yml opens its .md");

  // a path in the address naming a deck other than the kept one: chosen in the picker, checked by its last name
  await writeFile(page, "other-deck/talk.md", "# Other deck {#other}\n");
  await page.command("Page.navigate", { url: `${server.origin}/index.html?presentation=${encodeURIComponent('"/home/me/other-deck"')}` });
  const open = await buttonSaying(page, "Open /home/me/other-deck");
  expect(!!open, "a path naming a deck other than the kept one asks: 'Open /home/me/other-deck'");
  if (open) {
    await click(page, open); // the picker hands out live-deck
    const refusal = await page.waitFor("(document.querySelector('#gr4-reopen + p') || {}).textContent || ''", 15_000).catch(() => "");
    expect(refusal.includes("this is live-deck"), `a folder of another name is refused beside the button: ${refusal}`);
    await page.evaluate("globalThis.gr4PickerAnswer = 'other-deck'");
    await click(page, open);
    expect(await reached(page, "view=other", 60_000), "a second click, the folder of the address's name, opens the deck");
    expect(decodeURIComponent(await page.evaluate("location.search")).includes("presentation=/home/me/other-deck"), "and the address keeps the path");
  }
  await page.command("Page.reload");
  const reopen = await buttonSaying(page, "Reopen /home/me/other-deck");
  expect(!!reopen, "F5 finds the kept handle by the path's last name ('Reopen', as this browser asks again)");
  if (reopen) {
    await click(page, reopen);
    expect(await reached(page, "view=other", 60_000), "and reopens it");
  }

  if (failures.length > 0) {
    console.error("console output:\n  " + page.consoleLines.slice(-30).join("\n  "));
  }
  await page.close();
});

process.exit(failures.length === 0 ? 0 : 1);

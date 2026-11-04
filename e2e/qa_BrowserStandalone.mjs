// "download viewer + deck" hands over one archive: the site's files as served, the deck shown under default/, a start
// script per platform and a README. Unpacked and served by a plain static host (no headers), the page isolates itself
// through the worker, starts its threads, opens the deck without any address and reads nothing but the folder's files;
// a deck's .tar.gz beside it opens by `?presentation=`. Opened as a file it says what to start; started by
// launch-chromium.sh, a Chromium with two switches in a profile of its own, it presents from the folder with no server.

import { attachPage, openPage, startServer, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt } from "../devtools/png.mjs";
import { execFileSync, spawn } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserStandalone.mjs <viewer-web directory>");
  process.exit(2);
}
const deckDirectory = join(import.meta.dirname, "..", "test", "fixtures", "remote-deck"); // never staged with the viewer
const scratch = mkdtempSync(join(tmpdir(), "gr4-present-standalone-"));
const downloads = join(scratch, "downloads");
const carried = join(scratch, "carried"); // where a presenter unpacks the archive
execFileSync("mkdir", ["-p", downloads, carried]);

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const nextFrame = () => new Promise((resolve) => setTimeout(resolve, 100));
const click = async (page, point) => {
  await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: point.x, y: point.y });
  await nextFrame();
  for (const type of ["mousePressed", "mouseReleased"]) {
    await page.command("Input.dispatchMouseEvent", { type, x: point.x, y: point.y, button: "left", clickCount: 1 });
    await nextFrame();
  }
};
// the side menu slides out as the pointer nears the left edge; the foot's rows publish where they are drawn, with their labels
const utilityEntry = async (page, label) => {
  const probe = `(() => { const entry = (window.gr4FileEntries || []).find((e) => e[0] === "utility" && e[6].endsWith(${JSON.stringify(label)})); return entry && entry[2] >= 0 ? JSON.stringify(entry) : ""; })()`;
  for (let attempt = 0; attempt < 100; ++attempt) {
    await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 4 + (attempt % 2), y: 300 });
    const found = await page.evaluate(probe);
    if (found) {
      const entry = JSON.parse(found);
      return { x: entry[2] + entry[4] / 2, y: entry[3] + entry[5] / 2 };
    }
    await nextFrame();
  }
  throw new Error(`the side menu never showed ${label}`);
};
// the first file in `folder` whose name matches, once the browser has finished writing it (no .crdownload beside it)
const untilDownload = async (folder, pattern, timeoutMs = 120_000) => {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const names = readdirSync(folder);
    const done = names.find((name) => pattern.test(name));
    if (done && !names.some((name) => name.endsWith(".crdownload"))) {
      return done;
    }
    await nextFrame();
  }
  return "";
};
const kFigureTeal = { r: 0, g: 128, b: 128 }; // test/fixtures/remote-deck/figures/square.svg
const tealPixelsOn = async (page) => {
  const shot = decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
  let teal = 0;
  for (let y = 0; y < shot.height; y += 2) {
    for (let x = 0; x < shot.width; x += 2) {
      const pixel = pixelAt(shot, x, y);
      teal += ["r", "g", "b"].every((channel) => Math.abs(pixel[channel] - kFigureTeal[channel]) <= 12) ? 4 : 0;
    }
  }
  return teal;
};

try {
  await withViewer(directory, async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html`);
    expect(await page.waitFor("(globalThis.gr4Location || '').includes('view=')", 120_000), "the built-in deck opens");
    await page.command("Browser.setDownloadBehavior", { behavior: "allow", downloadPath: downloads });
    await page.evaluate("document.getElementById('canvas').focus()");
    await click(page, await utilityEntry(page, "download viewer + deck"));
    const archive = await untilDownload(downloads, /^gr4-present.*\.tar\.gz$/); // the viewer's own deck is named after it already, so no second gr4-present
    expect(archive !== "", `"download viewer + deck" saves one archive, gr4-present-<deck>.tar.gz (got ${readdirSync(downloads).join(", ") || "nothing"})`);
    expect(!page.consoleLines.some((line) => line.includes("could not be saved")), "without a problem reported");
    if (failures.length > 0) {
      console.error("console output:\n  " + page.consoleLines.slice(-30).join("\n  "));
    }
    await page.close();
    if (archive === "") {
      return;
    }
    const size = statSync(join(downloads, archive)).size;
    execFileSync("tar", ["-xzf", join(downloads, archive), "-C", carried]);
    const folder = join(carried, archive.replace(/\.tar\.gz$/, ""));
    const names = existsSync(folder) ? readdirSync(folder).sort() : [];
    const wanted = ["README.txt", "default", "gr4-serviceworker.js", "icon.png", "index.html", "index.js", "index.wasm", "launch-chromium.cmd", "launch-chromium.sh", "manifest.webmanifest", "serve.cmd", "serve.ps1", "serve.sh"];
    expect(wanted.every((name) => names.includes(name)), `${archive} (${(size / 1e6).toFixed(1)} MB) unpacks to ${archive.replace(/\.tar\.gz$/, "")}/ holding ${names.join(", ")}`);
    if (names.length === 0) {
      return;
    }
    expect(["serve.sh", "launch-chromium.sh"].every((name) => (statSync(join(folder, name)).mode & 0o111) !== 0), "serve.sh and launch-chromium.sh are executable");
    const parses = (command, args, input) => {
      try {
        execFileSync(command, args, { input, stdio: ["pipe", "pipe", "pipe"] });
        return true;
      } catch (error) {
        return error.code === "ENOENT" ? "skipped" : false; // no such interpreter here
      }
    };
    const serveSh = readFileSync(join(folder, "serve.sh"), "utf8");
    expect(["serve.sh", "launch-chromium.sh"].every((name) => parses("sh", ["-n", join(folder, name)]) === true), "serve.sh and launch-chromium.sh parse as POSIX sh");
    const python = serveSh.slice(serveSh.indexOf("<<'PY'\n") + 7, serveSh.indexOf("\nPY\n"));
    const pythonParses = parses("python3", ["-c", "import ast, sys; ast.parse(sys.stdin.read())"], python);
    expect(pythonParses !== false, `its python server parses${pythonParses === "skipped" ? " (no python3 here, skipped)" : ""}`);
    expect(["serve.cmd", "launch-chromium.cmd"].every((name) => readFileSync(join(folder, name), "utf8").includes("\r\n")), "the Windows scripts end their lines in CR LF");
    expect(existsSync(join(folder, "default", "index.yml")) && existsSync(join(folder, "default", "talk.md")), "the deck shown is under default/");
    const page1 = readFileSync(join(folder, "index.html"), "utf8");
    expect(page1.includes(`<meta name="gr4-site" content="${server.origin}" />`), "the page names the site it came from");
    expect(readFileSync(join(folder, "README.txt"), "utf8").includes(server.origin), "and so does the README");
    execFileSync("tar", ["-czf", join(folder, "remote-deck.tar.gz"), "-C", join(deckDirectory, ".."), "remote-deck"]);

    // a plain static host, as a presenter's own: no headers, every request on record
    const host = await startServer(folder, ["--no-isolation"]);
    try {
      const carriedPage = await openPage(browser, "about:blank");
      await carriedPage.command("Page.enable");
      await carriedPage.command("Network.enable");
      const reached = (expression, timeoutMs = 120_000) => carriedPage.waitFor(expression, timeoutMs).catch(() => false);
      await carriedPage.command("Page.navigate", { url: `${host.origin}/index.html` });
      expect(await reached("window.crossOriginIsolated === true"), "served without headers, the worker's reload makes the carried page cross-origin isolated");
      expect(await reached("(globalThis.gr4Location || '').includes('view=')"), "the deck opens without any address");
      expect(await reached("typeof PThread !== 'undefined' && PThread.unusedWorkers.concat(PThread.runningWorkers).some((worker) => worker.loaded)", 60_000), "pthread workers start");
      const requested = new Set();
      for (const event of carriedPage.eventsSeen()) {
        if (event.method === "Network.requestWillBeSent" && !event.params.request.url.startsWith("blob:")) {
          requested.add(decodeURIComponent(new URL(event.params.request.url).pathname));
        }
      }
      // the viewer asks every host once whether it relays; everything else must be a file of the folder
      const others = [...requested].filter((path) => path !== "/relay-link" && !existsSync(join(folder, path.replace(/^\//, "") || "index.html")));
      expect(requested.size > 3 && others.length === 0, `nothing outside the folder was requested (${requested.size} requests${others.length > 0 ? ", outside: " + others.join(", ") : ""})`);

      await carriedPage.evaluate("globalThis.gr4OldDocument = true");
      await carriedPage.command("Page.navigate", { url: `${host.origin}/index.html?presentation=remote-deck.tar.gz` });
      expect(await reached("!globalThis.gr4OldDocument && (globalThis.gr4Location || '').includes('view=remote&step=0')"), "a deck's .tar.gz beside the page opens from `?presentation=remote-deck.tar.gz`");
      let teal = 0;
      for (let attempt = 0; attempt < 40 && teal < 1000; ++attempt) {
        teal = await tealPixelsOn(carriedPage);
        await nextFrame();
      }
      expect(teal >= 1000, `its figure, unpacked from the archive, is drawn: ${teal} teal pixels`);
      if (failures.length > 0) {
        console.error("carried page status: " + (await carriedPage.evaluate("(document.getElementById('status') || {}).textContent || ''")));
        console.error("carried page console output:\n  " + carriedPage.consoleLines.slice(-40).join("\n  "));
      }
      await carriedPage.close();
    } finally {
      host.stop();
    }

    // opened by double-click: no SharedArrayBuffer anywhere, so the page says what to start, and where else it runs
    const asFile = await openPage(browser, `file://${join(folder, "index.html")}`);
    const status = await asFile.waitFor("((document.getElementById('status') || {}).textContent || '').startsWith('failed') && document.getElementById('status').textContent", 60_000).catch(() => "");
    expect(status.includes("serve.sh") && status.includes("launch-chromium.sh") && status.includes(server.origin), `as a file, the launch screen names the scripts and the site: ${JSON.stringify(status)}`);
    await asFile.close();

    // launch-chromium.sh as a presenter runs it, its browser made headless through GR4_PRESENT_BROWSER: the page opens
    // as a file, isolated it is not, yet threads run and the deck beside it opens; once the window closes, the
    // browser's profile is gone
    const port = 9800 + (process.pid % 150);
    const headless = join(scratch, "headless-chromium.sh");
    const switches = `--headless=new --use-gl=angle --use-angle=swiftshader --enable-unsafe-swiftshader --window-size=1280,800 --remote-debugging-port=${port}`;
    writeFileSync(headless, `#!/bin/sh\nfor b in google-chrome chromium chromium-browser google-chrome-stable; do command -v "$b" >/dev/null 2>&1 && exec "$b" ${switches} "$@"; done\nexit 127\n`, { mode: 0o755 });
    const profiles = join(scratch, "profiles");
    mkdirSync(profiles);
    const launched = async (deckArguments, body) => {
      const launcher = spawn("sh", [join(folder, "launch-chromium.sh"), ...deckArguments], { env: { ...process.env, GR4_PRESENT_BROWSER: headless, TMPDIR: profiles }, stdio: ["ignore", "pipe", "pipe"] });
      let output = "";
      launcher.stdout.on("data", (chunk) => (output += chunk));
      launcher.stderr.on("data", (chunk) => (output += chunk));
      const exited = new Promise((resolve) => launcher.once("exit", resolve));
      const endpoint = `http://127.0.0.1:${port}`;
      let target = null;
      for (let attempt = 0; attempt < 300 && !target && launcher.exitCode === null; ++attempt) {
        const pages = await fetch(`${endpoint}/json/list`).then((response) => response.json()).catch(() => []);
        target = pages.find((entry) => entry.type === "page" && entry.url.startsWith("file://")) || null;
        await nextFrame();
      }
      expect(target !== null, `launch-chromium.sh ${deckArguments.join(" ")} opens the page as a file${target ? ": " + target.url : ", output: " + output}`);
      if (!target) {
        launcher.kill();
        return;
      }
      const page = await attachPage({ endpoint }, target);
      await body(page);
      await page.command("Browser.close").catch(() => {});
      const code = await Promise.race([exited, new Promise((resolve) => setTimeout(() => resolve("still running"), 30_000))]);
      const left = readdirSync(profiles).filter((name) => name.startsWith("gr4-present-browser.")); // the browser keeps its own temporary files there too
      expect(code === 0 && left.length === 0, `once its window closes the script ends (${code}) and the browser's profile is gone (left: ${left.join(", ") || "nothing"})`);
      if (code === "still running") {
        launcher.kill();
      }
    };
    await launched([], async (page) => {
      const reached = (expression, timeoutMs = 120_000) => page.waitFor(expression, timeoutMs).catch(() => false);
      expect(await reached("(globalThis.gr4Location || '').includes('view=')"), "the deck under default/ opens without a server");
      expect(await reached("window.crossOriginIsolated === false && typeof PThread !== 'undefined' && PThread.unusedWorkers.concat(PThread.runningWorkers).some((worker) => worker.loaded)", 60_000), "threads run, without isolation");
      expect(readFileSync(join(folder, "default.files"), "utf8").split("\n").includes("index.yml"), "the script listed the deck's files for the page");
      if (failures.length > 0) {
        console.error("launched page status: " + (await page.evaluate("(document.getElementById('status') || {}).textContent || ''")));
      }
    });
    await launched(["remote-deck.tar.gz"], async (page) => {
      const opened = await page.waitFor("(globalThis.gr4Location || '').includes('view=remote&step=0')", 120_000).catch(() => false);
      let teal = 0;
      for (let attempt = 0; opened && attempt < 40 && teal < 1000; ++attempt) {
        teal = await tealPixelsOn(page);
        await nextFrame();
      }
      expect(opened && teal >= 1000, `launch-chromium.sh remote-deck.tar.gz opens that deck and draws its figure: ${teal} teal pixels`);
    });
  });
} finally {
  execFileSync("rm", ["-rf", scratch]);
}

process.exit(failures.length === 0 ? 0 : 1);

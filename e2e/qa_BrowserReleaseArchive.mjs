// The release archive devtools/ci/web-archive.sh makes of this build, as CI attaches it to a GitHub release:
// one folder holding only the viewer, the start scripts, README.txt and the one-slide stub deck;
// the stub deck opens through launch-chromium.sh and from a plain static host, where the service worker supplies the isolation headers;
// the deck-site action's script lays another deck in place of the stub and refuses an archive whose checksum does not match.
// Ground truth: the file list kSiteFiles and the bundle's scripts make, and the stub deck's own words naming the version.

import { attachPage, launchBrowser, openPage, startServer, withOpenDigitizer } from "../devtools/browser_harness.mjs";
import { execFileSync, spawn } from "node:child_process";
import { mkdirSync, mkdtempSync, readdirSync, rmSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserReleaseArchive.mjs <viewer-web directory>");
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

const flavour = !withOpenDigitizer ? "minimal" : "full";
const version = "v0.0.0-qa";
const name = `gr4-present-web-${flavour}-${version}`;
const scratch = mkdtempSync(join(tmpdir(), "gr4-present-release-"));
const showsStub = "window.gr4TextRuns && window.gr4TextRuns.some((run) => run[4].includes('v0.0.0-qa')) && (globalThis.gr4Location || '').includes('view=viewer') ? 1 : false";

try {
  execFileSync("bash", [join(import.meta.dirname, "..", "devtools", "ci", "web-archive.sh"), dirname(directory), flavour, version, scratch], {
    env: { ...process.env, GR4_PRESENT_BUILD: "built for qa" },
    stdio: ["ignore", "pipe", "inherit"],
  });
  const archive = join(scratch, `${name}.tar.gz`);
  execFileSync("tar", ["-xzf", archive, "-C", scratch]);
  const folder = join(scratch, name);
  const listed = (at, prefix = "") =>
    readdirSync(at, { withFileTypes: true }).flatMap((entry) => (entry.isDirectory() ? listed(join(at, entry.name), `${prefix}${entry.name}/`) : [`${prefix}${entry.name}`]));
  const files = listed(folder).sort();
  const wanted = [
    "README.txt",
    "default/index.yml",
    "default/talk.md",
    "gr4-serviceworker.js",
    "icon.png",
    "index.html",
    "index.js",
    "index.wasm",
    "launch-chromium.cmd",
    "launch-chromium.sh",
    "manifest.webmanifest",
    "serve.cmd",
    "serve.ps1",
    "serve.sh",
  ];
  expect(
    JSON.stringify(files) === JSON.stringify(wanted),
    `${name}.tar.gz (${(statSync(archive).size / 1e6).toFixed(1)} MB) holds the viewer, its scripts, README.txt and the stub deck, nothing else: ${files.join(", ")}`,
  );
  expect(["serve.sh", "launch-chromium.sh"].every((file) => (statSync(join(folder, file)).mode & 0o111) !== 0), "its shell scripts are executable");

  // as a presenter starts it offline: launch-chromium.sh, its browser made headless through GR4_PRESENT_BROWSER
  {
    const port = 9650 + (process.pid % 150);
    const headless = join(scratch, "headless-chromium.sh");
    const switches = `--headless=new --use-gl=angle --use-angle=swiftshader --enable-unsafe-swiftshader --window-size=1280,800 --remote-debugging-port=${port}`;
    writeFileSync(headless, `#!/bin/sh\nfor b in google-chrome chromium chromium-browser google-chrome-stable; do command -v "$b" >/dev/null 2>&1 && exec "$b" ${switches} "$@"; done\nexit 127\n`, { mode: 0o755 });
    const profiles = join(scratch, "profiles");
    mkdirSync(profiles);
    const launcher = spawn("sh", [join(folder, "launch-chromium.sh")], { env: { ...process.env, GR4_PRESENT_BROWSER: headless, TMPDIR: profiles }, stdio: "ignore" });
    const endpoint = `http://127.0.0.1:${port}`;
    let target = null;
    for (let attempt = 0; attempt < 300 && !target && launcher.exitCode === null; ++attempt) {
      const pages = await fetch(`${endpoint}/json/list`)
        .then((response) => response.json())
        .catch(() => []);
      target = pages.find((entry) => entry.type === "page" && entry.url.startsWith("file://")) || null;
      await nextFrame();
    }
    if (target) {
      const page = await attachPage({ endpoint }, target);
      expect(Boolean(await page.waitFor(showsStub, 120_000).catch(() => false)), `launch-chromium.sh opens the stub deck, naming ${version}, from ${target.url}`);
      await page.command("Browser.close").catch(() => {});
    } else {
      expect(false, "launch-chromium.sh opens the page as a file");
    }
    launcher.kill();
  }

  // as another repository's GitHub Pages serves the folder: no isolation headers, the service worker adds them
  {
    const server = await startServer(folder, ["--no-isolation"]);
    const browser = await launchBrowser();
    try {
      const page = await openPage(browser, `${server.origin}/index.html?mode=windowed`);
      expect(Boolean(await page.waitFor(showsStub, 120_000).catch(() => false)), "a plain static host shows the stub deck");
      expect((await page.evaluate("window.crossOriginIsolated === true ? 1 : 0")) === 1, "isolated by the service worker, as threads need");
      await page.close();
    } finally {
      await browser.stop();
      server.stop();
    }
  }

  // the deck-site action's script, as another repository's workflow runs it: this archive with a deck of its own as
  // default/, served from a plain static host
  {
    const sums = join(scratch, "SHA256SUMS");
    writeFileSync(sums, execFileSync("sha256sum", [`${name}.tar.gz`], { cwd: scratch }));
    const site = join(scratch, "site");
    const deck = join(import.meta.dirname, "..", "test", "fixtures", "remote-deck");
    execFileSync("bash", [join(import.meta.dirname, "..", ".github", "actions", "deck-site", "deck-site.sh"), deck, flavour, version, site], {
      env: { ...process.env, GR4_PRESENT_ARCHIVE: archive, GR4_PRESENT_SUMS: sums },
      stdio: ["ignore", "pipe", "inherit"],
    });
    expect(listed(join(site, "default")).sort().join(",") === listed(deck).sort().join(","), "deck-site.sh puts the deck in place of the stub deck");
    const server = await startServer(site, ["--no-isolation"]);
    const browser = await launchBrowser();
    try {
      const page = await openPage(browser, `${server.origin}/index.html?mode=windowed`);
      expect(Boolean(await page.waitFor("(globalThis.gr4Location || '').includes('view=remote') ? 1 : false", 120_000).catch(() => false)), "and the site opens that deck without an address");
      await page.close();
    } finally {
      await browser.stop();
      server.stop();
    }
    writeFileSync(sums, "0000  " + `${name}.tar.gz\n`);
    let refused = false;
    try {
      execFileSync("bash", [join(import.meta.dirname, "..", ".github", "actions", "deck-site", "deck-site.sh"), deck, flavour, version, join(scratch, "refused")], {
        env: { ...process.env, GR4_PRESENT_ARCHIVE: archive, GR4_PRESENT_SUMS: sums },
        stdio: "ignore",
      });
    } catch {
      refused = true;
    }
    expect(refused, "an archive whose checksum does not match is refused");
  }
} finally {
  rmSync(scratch, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserReleaseArchive: all checks passed" : `qa_BrowserReleaseArchive: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

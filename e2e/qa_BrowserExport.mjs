// The browser's PDF export (`?export=slides`) through GR4's fileio; an audience tab open beside it must not move
// Ground truth: the deck's Markdown, one page per level-one heading outside fenced code, every heading readable as text
// the PDF is read back by Poppler, independently of the writer

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readdirSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserExport.mjs <viewer-web directory>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

/// level-one headings outside fenced code, without sub-title or anchor
const sectionTitles = (markdown) => {
  const titles = [];
  let fenced = false;
  for (const line of markdown.split("\n")) {
    if (/^(```|~~~)/.test(line)) {
      fenced = !fenced;
    } else if (!fenced && line.startsWith("# ")) {
      titles.push(
        line
          .slice(2)
          .replace(/<br>.*$/, "")
          .replace(/\s*\{#[^}]*\}\s*$/, "")
          .trim(),
      );
    }
  }
  return titles;
};

const downloads = mkdtempSync(join(tmpdir(), "gr4-present-export-"));
try {
  await withViewer(directory, async ({ server, browser }) => {
    const audience = await openPage(browser, `${server.origin}/index.html#view=markdown&step=0`);
    await audience.waitFor("(globalThis.gr4Location || '').includes('view=markdown') && window.gr4Zoom ? 1 : false");

    const page = await openPage(browser, "about:blank");
    await page.command("Browser.setDownloadBehavior", { behavior: "allow", downloadPath: downloads });
    const started = Date.now();
    await page.command("Page.navigate", { url: `${server.origin}/index.html?export=slides` });
    let outcome = null;
    while (Date.now() - started < 240000 && outcome === null) {
      await page.command("Page.captureScreenshot", { format: "png" });
      outcome = await page.evaluate("globalThis.gr4Export ? JSON.stringify(globalThis.gr4Export) : null");
    }
    const report = JSON.parse(outcome ?? "{}");
    expect(
      typeof report.outcome === "string" && report.outcome.startsWith("exported"),
      `the export says it is done: ${outcome} after ${((Date.now() - started) / 1000).toFixed(0)} s`,
    );

    let file = "";
    for (let wait = 0; wait < 100 && !file; ++wait) {
      file = readdirSync(downloads).find((name) => name.endsWith(".pdf")) ?? "";
      if (!file) {
        await page.command("Page.captureScreenshot", { format: "png" });
      }
    }
    expect(file === "gr4-present-slides.pdf", `the browser saved it as the deck's title says: ${file || "nothing"}`);
    expect((await audience.evaluate("(globalThis.gr4Location || '')")).includes("view=markdown"), "and the audience tab stayed on its slide");

    if (file && existsSync(join(downloads, file))) {
      const path = join(downloads, file);
      const titles = sectionTitles(readFileSync(join(directory, "default", "talk.md"), "utf8"));
      const info = execFileSync("pdfinfo", [path], { encoding: "utf8" });
      const pages = Number(/Pages:\s+(\d+)/.exec(info)?.[1] ?? -1);
      expect(pages === titles.length, `one page per section: ${pages} pages for ${titles.length} sections`);
      expect(/Page size:\s+960 x 540 pts/.test(info), "pages are 13.33 by 7.5 in");
      const text = execFileSync("pdftotext", ["-enc", "UTF-8", path, "-"], { encoding: "utf8" }).replace(/\s+/g, " ");
      const missing = titles.filter((title) => !text.includes(title));
      expect(missing.length === 0, `every heading is text in the PDF${missing.length ? `, missing: ${missing.join("; ")}` : ""}`);
      // formula pages embed no image yet carry dark pixels outside every word's box, which only outlines explain
      const pageCount = pages;
      let formulasPage = 0;
      for (let at = 1; at <= pageCount && !formulasPage; ++at) {
        const first = execFileSync("pdftotext", ["-f", `${at}`, "-l", `${at}`, path, "-"], { encoding: "utf8" })
          .trim()
          .split("\n")[0];
        formulasPage = first.startsWith("LaTeX Formulas") ? at : 0;
      }
      expect(formulasPage > 0, `the deck has its Formulas page, page ${formulasPage}`);
      if (formulasPage > 0) {
        const range = ["-f", `${formulasPage}`, "-l", `${formulasPage}`];
        const images =
          execFileSync("pdfimages", ["-list", ...range, path], { encoding: "utf8" })
            .trim()
            .split("\n").length - 2;
        expect(images === 0, `the Formulas page embeds no picture: ${images}`);
        const prefix = join(downloads, "formulas");
        execFileSync("pdftoppm", ["-r", "36", "-gray", "-singlefile", ...range, path, prefix]);
        const pgm = readFileSync(`${prefix}.pgm`);
        const header = /^P5\s+(\d+)\s+(\d+)\s+255\s/.exec(pgm.subarray(0, 32).toString("latin1"));
        const [width, height] = [Number(header[1]), Number(header[2])];
        const pixels = pgm.subarray(header[0].length);
        const words = [
          ...execFileSync("pdftotext", ["-bbox", ...range, path, "-"], { encoding: "utf8" }).matchAll(
            /xMin="([\d.]+)" yMin="([\d.]+)" xMax="([\d.]+)" yMax="([\d.]+)"/g,
          ),
        ].map((m) => m.slice(1).map((v) => Number(v) * 0.5)); // points to pixels at 36 dpi
        let unexplained = 0;
        for (let y = 0; y < height; ++y) {
          for (let x = 0; x < width; ++x) {
            if (pixels[y * width + x] < 128 && !words.some(([x0, y0, x1, y1]) => x >= x0 - 1 && x <= x1 + 1 && y >= y0 - 1 && y <= y1 + 1)) {
              ++unexplained;
            }
          }
        }
        expect(unexplained > 150, `and ink no word explains, drawn by the formulas: ${unexplained} pixels at 36 dpi`);
      }
      // drawings are outlines: no embedded image, and the labels written in facility.svg are searchable text
      const pageTitled = (prefix) => {
        for (let at = 1; at <= pageCount; ++at) {
          const first = execFileSync("pdftotext", ["-f", `${at}`, "-l", `${at}`, path, "-"], { encoding: "utf8" })
            .trim()
            .split("\n")[0];
          if (first.startsWith(prefix)) {
            return at;
          }
        }
        return 0;
      };
      for (const [title, words] of [
        ["SVG Slide Master", []],
        ["SVG-based Animations", ["injector", "RFQ", "DTL"]],
      ]) {
        const at = pageTitled(title);
        const range = ["-f", `${at}`, "-l", `${at}`];
        const images = at
          ? execFileSync("pdfimages", ["-list", ...range, path], { encoding: "utf8" })
              .trim()
              .split("\n").length - 2
          : -1;
        const text = at ? execFileSync("pdftotext", [...range, path, "-"], { encoding: "utf8" }) : "";
        expect(
          at > 0 && images === 0 && words.every((word) => text.includes(word)),
          `"${title}" (page ${at}) draws its SVG as paths, ${images} pictures${words.length ? `, with its labels ${words.join(", ")} as text` : ""}`,
        );
      }
      // pictures stay pictures, the Solvay photograph from its own file, wider than the 1920-pixel recorded frame
      const imagesOn = (at) =>
        execFileSync("pdfimages", ["-list", "-f", `${at}`, "-l", `${at}`, path], { encoding: "utf8" })
          .trim()
          .split("\n")
          .slice(2)
          .map((line) => line.trim().split(/\s+/));
      const picturesPage = pageTitled("Pictures");
      expect(
        picturesPage > 0 && imagesOn(picturesPage).length >= 4,
        `the Pictures page embeds its pictures: ${picturesPage ? imagesOn(picturesPage).length : 0}`,
      );
      const videoPage = pageTitled("Video");
      expect(videoPage > 0 && imagesOn(videoPage).length === 1, `the Video page embeds the clip's frame: ${videoPage ? imagesOn(videoPage).length : 0}`);
      // effects are pictures of what they rendered, read back from their own textures: one picture each
      for (const [title, words] of [
        ["Background", ["background runs on"]],
        ["Viewport", []],
        ["Slide Transitions", ["says how it arrives"]],
      ]) {
        const at = pageTitled(title);
        const text = at ? execFileSync("pdftotext", ["-f", `${at}`, "-l", `${at}`, path, "-"], { encoding: "utf8" }).replace(/\s+/g, " ") : "";
        expect(
          at > 0 && imagesOn(at).length === 1 && words.every((word) => text.includes(word)),
          `"${title}" (page ${at}) embeds its effect as one picture, ${at ? imagesOn(at).length : 0}${words.length ? `, its words as text` : ""}`,
        );
      }
      // a region needing a device runs its standby in an export: charts weigh far more than an empty region's few kB
      const microphonePage = (() => {
        for (let at = 1; at <= pageCount; ++at) {
          if (execFileSync("pdftotext", ["-f", `${at}`, "-l", `${at}`, path, "-"], { encoding: "utf8" }).includes("Schmitt")) {
            return at;
          }
        }
        return 0;
      })();
      const largest = microphonePage
        ? Math.max(
            0,
            ...imagesOn(microphonePage).map(
              (columns) => Number.parseFloat(columns[14]) * (columns[14].endsWith("M") ? 1024 : columns[14].endsWith("K") ? 1 : 1 / 1024),
            ),
          )
        : 0;
      expect(largest > 50, `the microphone page shows its standby's charts, the largest picture ${largest.toFixed(0)} kB`);
      const solvayPage = pageTitled("5th Solvay Conference");
      const widest = solvayPage ? Math.max(0, ...imagesOn(solvayPage).map((columns) => Number(columns[3]))) : 0;
      expect(widest > 1920, `the Solvay photograph is embedded from its file, ${widest} pixels wide`);
      // Ground truth: the deck's Markdown gives the comment (noted sections), bookmark (sections) and QR link counts
      const raw = readFileSync(path).toString("latin1");
      const markdown = readFileSync(join(directory, "default", "talk.md"), "utf8");
      let notedSections = 0;
      let fencedNotes = false;
      let sectionHasNotes = false;
      for (const line of markdown.split("\n")) {
        if (/^(```|~~~)/.test(line)) {
          fencedNotes = !fencedNotes;
        } else if (!fencedNotes && line.startsWith("# ")) {
          notedSections += sectionHasNotes ? 1 : 0;
          sectionHasNotes = false;
        } else if (!fencedNotes && line.startsWith(":::notes")) {
          sectionHasNotes = true;
        }
      }
      notedSections += sectionHasNotes ? 1 : 0;
      const comments = (raw.match(/\/Subtype \/Text/g) ?? []).length;
      expect(comments === notedSections, `a comment for every section with notes: ${comments} for ${notedSections}`);
      const bookmarks = (raw.match(/\/Title /g) ?? []).length - 1; // the document's own title is one more
      expect(bookmarks === titles.length, `a bookmark for every section: ${bookmarks} for ${titles.length}`);
      // in a browser the codes encode where the deck was opened from, which this test chose: its index page
      const qr = `${server.origin}/`;
      const uris = [...raw.matchAll(/\/URI \(((?:\\[0-7]{3}|[^)])*)\)/g)].map((m) =>
        m[1].replace(/\\([0-7]{3})/g, (_, octal) => String.fromCharCode(parseInt(octal, 8))),
      );
      expect(qr !== "" && uris.includes(qr), `the references' QR code links to ${qr}: ${uris.join(", ")}`);
      for (const mode of ["slides", "steps"]) {
        const pdf = `${qr}${qr.includes("?") ? "&" : "?"}export=${mode}`;
        expect(uris.includes(pdf), `the references lead to the deck's PDF, ${mode}: ${pdf} in ${uris.join(", ")}`);
      }
      const fonts = execFileSync("pdffonts", [path], { encoding: "utf8" });
      expect(
        fonts.includes("LiberationSans") &&
          fonts
            .split("\n")
            .slice(2)
            .every((line) => !line.trim() || / yes yes yes /.test(line)),
        "every face is embedded, subset and mapped to Unicode",
      );
    }
    await page.close();

    // a live chart is taken running, so a second export differs; the audio slide's recording stays byte for byte
    if (file) {
      const again = join(downloads, "again");
      execFileSync("mkdir", ["-p", again]);
      const second = await openPage(browser, "about:blank");
      await second.command("Browser.setDownloadBehavior", { behavior: "allow", downloadPath: again });
      await second.command("Page.navigate", { url: `${server.origin}/index.html?export=slides` });
      let done = null;
      for (const started = Date.now(); Date.now() - started < 240000 && done === null;) {
        await second.command("Page.captureScreenshot", { format: "png" });
        done = await second.evaluate("globalThis.gr4Export ? 1 : null");
      }
      let secondFile = "";
      for (let wait = 0; wait < 100 && !secondFile; ++wait) {
        secondFile = readdirSync(again).find((name) => name.endsWith(".pdf")) ?? "";
        if (!secondFile) {
          await second.command("Page.captureScreenshot", { format: "png" });
        }
      }
      await second.close();
      // the page by words of its title or subtitle: several live pages share the title "Live Chart"
      const chartOf = (pdf, pageWords, tag) => {
        const pages = execFileSync("pdftotext", [pdf, "-"], { encoding: "utf8" }).split("\f");
        const at = pages.findIndex((text) => text.includes(pageWords)) + 1;
        if (!at) {
          return "";
        }
        execFileSync("pdfimages", ["-f", `${at}`, "-l", `${at}`, "-png", pdf, join(downloads, tag)]);
        const first = readdirSync(downloads).find((name) => name.startsWith(`${tag}-`) && name.endsWith(".png"));
        return first ? readFileSync(join(downloads, first)).toString("base64") : "";
      };
      const path = join(downloads, file);
      const otherPath = join(again, secondFile);
      const liveFirst = chartOf(path, "Live Charts", "live-a");
      const liveSecond = secondFile ? chartOf(otherPath, "Live Charts", "live-b") : "";
      expect(liveFirst !== "" && liveSecond !== "" && liveFirst !== liveSecond, "a live chart is taken running: two exports catch it at different moments");
      const recordedFirst = chartOf(path, "Microphone Source", "rec-a");
      const recordedSecond = secondFile ? chartOf(otherPath, "Microphone Source", "rec-b") : "";
      // a region that needs a device runs its standby in an export, so it is a chart in both, taken running
      expect(recordedFirst !== "" && recordedSecond !== "", "and a region that needs a device shows its standby's chart in both");
    }
    await audience.close();
  });
} finally {
  rmSync(downloads, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserExport: all checks passed" : `qa_BrowserExport: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

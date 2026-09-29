// A chart fills the box its slide gave it, in both directions, and is set in the deck's type ladder.
//
// The box is what the viewer published for the region (`window.gr4Regions`), so the geometry compared against is
// the one the layout used and not one re-derived here. What the chart drew is read off a screenshot: the bounding
// box of everything on the slide that is not its background. The chart's axis labels are part of what it drew, so
// they are inside that bounding box and it is the whole child that is compared, not the plot alone.
//
// Three window shapes, because a chart that fills at 16:9 by accident of its default size shows itself at the
// others. The left edge is not measured: the side menu opens over it and a synthesised pointer does not reliably
// close it, so only the top, bottom and right edges of the ink are compared.
//
// The type sizes are compared against the ladder as the deck states it -- 15 pt for a chart's axes and legend, the
// 12 pt floor for the tags it draws, 1 pt being 1.333 px at 1280 x 720 and following the diagonal from there --
// which is a statement in points, not something read out of the code under test.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng, pixelAt } from "../devtools/png.mjs";

const directory = process.argv[2];
if (!directory) {
    console.error("usage: qa_BrowserCharts.mjs <viewer-web directory>");
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
const kMenuEdge = 0.36; // of the width, as in the other browser tests
const kEdgeSlack = 0.05; // of the box: ink may stop this short of an edge
const kInkDistance = 60; // summed channel difference from the background that counts as drawn
// A time axis draws its tick labels only when it has some to show, and they take up to two lines below the plot,
// so what is drawn can stop short of the bottom edge by that much and still be the whole box. A chart fixed at one size would
// miss by far more at one of the three shapes.
const kBottomSlack = 0.2; // measured up to 0.18 on the smallest chart
const kTypeTolerance = 0.03;

const kShapes = [
    [1280, 720],
    [1000, 800],
    [1600, 700],
];
// in deck order: the walk only goes forwards
const kSlides = [
    { view: "live", region: "charts", aspect: 4 / 3 },
    { view: "live-timing", region: "timing", aspect: 0 },
    { view: "live-spectrum", region: "spectrum", aspect: 0 },
];

const viewOf = async (page) => (await page.evaluate("(globalThis.gr4Location || '')")).replace(/^#view=/, "").replace(/&.*$/, "");
const shotOf = async (page) => decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));

const walkTo = async (page, wanted) => {
    for (let step = 0; step < 120; ++step) {
        if ((await viewOf(page)) === wanted) {
            return true;
        }
        const before = await page.evaluate("(globalThis.gr4Location || '')");
        await page.press(...kRight);
        if (!(await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? (globalThis.gr4Location || '') : false`))) {
            return false;
        }
    }
    return false;
};

const apartness = (left, right) => Math.abs(left.r - right.r) + Math.abs(left.g - right.g) + Math.abs(left.b - right.b);

/// the top, bottom and right edges of what is drawn inside the box, by difference from the slide behind it
const inkWithin = (image, box) => {
    const [boxX, boxY, boxWidth, boxHeight] = box;
    const background = pixelAt(image, Math.min(image.width - 1, boxX + boxWidth + 6), boxY + Math.floor(boxHeight / 2));
    const left = Math.max(boxX, Math.floor(image.width * kMenuEdge));
    let top = Infinity;
    let bottom = -Infinity;
    let right = -Infinity;
    for (let y = boxY; y < boxY + boxHeight; ++y) {
        for (let x = left; x < boxX + boxWidth; ++x) {
            if (apartness(pixelAt(image, x, y), background) > kInkDistance) {
                top = Math.min(top, y);
                bottom = Math.max(bottom, y);
                right = Math.max(right, x);
            }
        }
    }
    return { top, bottom, right };
};

// A plot written into the slide, with nothing after it, takes the room down to the footer. Measured as the lowest ink
// between the paragraph and the footer band, which starts at 93 % of the height.
const kFooterStart = 0.93;
const kInlineReach = 0.82; // of the height: the axis label of a chart that fills the room ends near 0.85
await withViewer(directory, async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html`);
    await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
    await page.waitFor("(globalThis.gr4Location || '').includes('view=') && (globalThis.gr4Location || '').includes('step=0')");
    await page.evaluate("document.getElementById('canvas').focus()");
    await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 1024, y: 648 });
    expect(await walkTo(page, "inline-plot"), "inline-plot: reached");
    let lowest = -Infinity;
    let steady = 0;
    for (let attempt = 0; attempt < 600 && steady < 8; ++attempt) {
        const image = await shotOf(page);
        const background = pixelAt(image, 4, 4);
        let now = -Infinity;
        for (let y = Math.floor(0.3 * image.height); y < Math.floor(kFooterStart * image.height); ++y) {
            for (let x = Math.floor(kMenuEdge * image.width); x < image.width - 8; ++x) {
                if (apartness(pixelAt(image, x, y), background) > kInkDistance) {
                    now = y;
                }
            }
        }
        steady = now === lowest && now > 0 ? steady + 1 : 0;
        lowest = now;
    }
    expect(lowest >= kInlineReach * 720, `inline-plot: the charts reach ${lowest} px of 720, wanted at least ${kInlineReach * 720}`);
    await page.close();
});

for (const [width, height] of kShapes) {
    await withViewer(directory, async ({ server, browser }) => {
        const page = await openPage(browser, `${server.origin}/index.html`);
        await page.command("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor: 1, mobile: false });
        await page.waitFor("(globalThis.gr4Location || '').includes('view=') && (globalThis.gr4Location || '').includes('step=0')");
        await page.evaluate("document.getElementById('canvas').focus()");
        await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: Math.floor(width * 0.8), y: Math.floor(height * 0.9) });

        for (const { view, region, aspect } of kSlides) {
            const label = `${view} at ${width}x${height}`;
            expect(await walkTo(page, view), `${label}: reached`);
            const box = await page.waitFor(`window.gr4Regions && window.gr4Regions[${JSON.stringify(region)}]`);
            expect(Boolean(box), `${label}: the viewer publishes the region ${region}`);
            if (!box) {
                continue;
            }

            // A graph is built between frames and its chart draws once it is running, and the slide before is still
            // sliding out for a moment after arriving. So the ink is taken once it has stopped moving, not after a time.
            let ink = { top: Infinity, bottom: -Infinity, right: -Infinity };
            let steady = 0;
            for (let attempt = 0; attempt < 600 && steady < 8; ++attempt) {
                const now = inkWithin(await shotOf(page), box);
                steady = now.top === ink.top && now.bottom === ink.bottom && now.right === ink.right && now.bottom > now.top ? steady + 1 : 0;
                ink = now;
            }
            const [boxX, boxY, boxWidth, boxHeight] = box;
            const inkHeight = ink.bottom - ink.top + 1;

            // A stated shape is the largest of that shape that fits, centred; otherwise the whole box is the chart's
            const drawnWidth = aspect === 0 ? boxWidth : Math.min(boxWidth, aspect * boxHeight);
            const drawnHeight = aspect === 0 ? boxHeight : drawnWidth / aspect;
            const drawnRight = boxX + (boxWidth + drawnWidth) / 2;
            const drawnTop = boxY + (boxHeight - drawnHeight) / 2;
            expect(Math.abs(ink.top - drawnTop) <= kEdgeSlack * drawnHeight, `${label}: the chart starts at the top of its box, ink from ${ink.top}, box from ${drawnTop.toFixed(0)}`);
            expect(inkHeight >= (1 - kBottomSlack) * drawnHeight && inkHeight <= drawnHeight + 2, `${label}: the chart fills its height, ink ${inkHeight} px of ${drawnHeight.toFixed(0)}`);
            expect(ink.right >= drawnRight - kEdgeSlack * drawnWidth && ink.right <= drawnRight + 2, `${label}: the chart fills its width, ink to ${ink.right}, box to ${drawnRight.toFixed(0)}`);

            const [body, small, tiny] = await page.evaluate("window.gr4ChartType");
            const pixelsPerPoint = (1.333 * Math.hypot(width, height)) / Math.hypot(1280, 720);
            expect(Math.abs(body - 15 * pixelsPerPoint) <= kTypeTolerance * 15 * pixelsPerPoint, `${label}: chart text is 15 pt, ${body.toFixed(1)} px against ${(15 * pixelsPerPoint).toFixed(1)}`);
            // OpenDigitizer scales its small and tiny faces from that by its own load-time ratios to its normal face,
            // which are the rows of its LookAndFeel.cpp size table; a ratio outside them means the faces were resized
            const kOpenDigitizerRatios = [[17, 12, 20], [13, 8, 16], [18, 12, 22], [16, 12, 18]].map(([s, t, n]) => [s / n, t / n]);
            expect(
                kOpenDigitizerRatios.some(([s, t]) => Math.abs(small - s) < 1e-3 && Math.abs(tiny - t) < 1e-3),
                `${label}: chart labels keep OpenDigitizer's own ratios, ${small.toFixed(3)} and ${tiny.toFixed(3)} of its normal face`,
            );
        }
        if (failures.length > 0) {
            console.error("console output:\n  " + page.consoleLines.slice(-20).join("\n  "));
        }
        await page.close();
    });
}

console.log(failures.length === 0 ? "qa_BrowserCharts: all checks passed" : `qa_BrowserCharts: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

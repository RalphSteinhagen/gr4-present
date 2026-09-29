// Stepping back and forth across the live slides must not stall.
//
// A live region owns a GNU Radio graph, a scheduler and a thread. The deck holds a window of them around the
// cursor, and anything outside that window is destroyed -- on the UI thread, because that is where the frame is
// drawn. Destroying one waits for its scheduler to leave the states it may not be stopped in and then joins its
// thread, so a window that is too small makes every step backwards a teardown and a rebuild in the middle of a
// frame. In the browser, where the pthread pool is fixed and nothing is proxied off the main thread, that is a
// stall the audience sees.
//
// What is asserted is the thing that was reported: walk forwards across the live slides, walk back, and do it
// again, and the deck must still be drawing and its charts must still have data. Ground truth is the deck's own
// published cursor for where we are, and the canvas for whether anything is on it -- not a frame rate, which is
// the machine's business and not a contract.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";

const directory = process.argv[2];
if (!directory) {
    console.error("usage: qa_BrowserLiveWalk.mjs <viewer-web directory>");
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

/// the views carrying `:::gr4` regions, in deck order
const kLive = ["live", "live-timing", "live-spectrum", "live-graph"];

const viewOf = async (page) => (await page.evaluate("(globalThis.gr4Location || '')")).replace(/^#view=/, "").replace(/&.*$/, "");

/// presses `key` until the cursor names `wanted`, and says how many frames it took to get there
const walkTo = async (page, key, wanted, budget = 40) => {
    for (let step = 0; step < budget; ++step) {
        if ((await viewOf(page)) === wanted) {
            return step;
        }
        const before = await page.evaluate("(globalThis.gr4Location || '')");
        await page.press(...key);
        // a stalled viewer stops publishing its cursor, which is what the deadline inside waitFor catches
        const moved = await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? (globalThis.gr4Location || '') : false`);
        if (!moved) {
            return -1;
        }
    }
    return -1;
};

/// the canvas as it stands, so two of them can be compared; a stalled runtime keeps painting its last frame, so
/// "is anything lit" cannot tell a running deck from a frozen one and only a difference can
const frameOf = async (page) => (await page.command("Page.captureScreenshot", { format: "png" })).result.data;

/// how much of the canvas is not the slide's near-black ground, as a fraction; a blank or frozen-black view is 0
const inkFraction = async (page) => {
    const shot = (await page.command("Page.captureScreenshot", { format: "png" })).result.data;
    return await page.evaluate(`(async () => {
        const image = new Image();
        image.src = "data:image/png;base64,${shot}";
        await image.decode();
        const canvas = document.createElement("canvas");
        canvas.width = image.width; canvas.height = image.height;
        const context = canvas.getContext("2d");
        context.drawImage(image, 0, 0);
        const pixels = context.getImageData(0, 0, canvas.width, canvas.height).data;
        let lit = 0;
        for (let at = 0; at < pixels.length; at += 4) {
            if (pixels[at] + pixels[at + 1] + pixels[at + 2] > 120) { lit += 1; }
        }
        return lit / (pixels.length / 4);
    })()`);
};

/**
 * Asserts the slide is both painted and moving.
 *
 * A live chart never stands still: its traces advance every frame it is given. So two frames taken a moment apart
 * must differ. That is what separates a running deck from one whose runtime has stalled with its last frame still
 * on the canvas -- which is the failure being hunted, and which a brightness test cannot see.
 */
const assertLive = async (page, what) => {
    // Each screenshot is a round trip through the debugger and costs tens of milliseconds, so the counts here are
    // kept small on purpose: an earlier version took about a hundred per slide and the walk ran out of its budget
    // before it finished. A dozen frames is enough for a graph to produce, and a handful more to show movement.
    for (let frame = 0; frame < 12; ++frame) {
        await page.command("Page.captureScreenshot", { format: "png" });
    }
    const ink = await inkFraction(page);
    expect(ink > 0.02, `${what} is painted, ${(ink * 100).toFixed(1)}% of the canvas is lit`);

    const first = await frameOf(page);
    let moved = false;
    for (let attempt = 0; attempt < 12 && !moved; ++attempt) {
        moved = (await frameOf(page)) !== first;
    }
    expect(moved, `${what} is still running, its picture changes between frames`);
};

await withViewer(directory, async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html#view=rejoin&step=0`);
    await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
    // the link's own fragment is there before the deck has loaded, so the deck's published state is waited for
    expect(await page.waitFor("(globalThis.gr4Location || '').includes('view=') && (globalThis.gr4Location || '').includes('step=0') && window.gr4Zoom ? 1 : false"), "the viewer publishes its starting cursor");
    await page.evaluate("document.getElementById('canvas').focus()");
    // parked away from the left edge, or the side menu opens over the slide and counts as ink
    await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 900, y: 690 });

    // two passes, because the first visit builds a graph and the second has to reuse or rebuild one: the reported
    // stall is on the way back, not on the way out
    for (let pass = 1; pass <= 2; ++pass) {
        for (const view of kLive) {
            const began = Date.now();
            const reached = await walkTo(page, kRight, view);
            const took = Date.now() - began;
            expect(reached >= 0, `pass ${pass}: forwards to ${view}`);
            if (reached < 0) {
                break;
            }
            expect(took < 15000, `pass ${pass}: reaching ${view} took ${(took / 1000).toFixed(1)} s`);
            await assertLive(page, `pass ${pass}: ${view}`);
        }

        for (const view of [...kLive].reverse().slice(1)) {
            const began = Date.now();
            const reached = await walkTo(page, kLeft, view);
            const took = Date.now() - began;
            expect(reached >= 0, `pass ${pass}: backwards to ${view}`);
            if (reached < 0) {
                break;
            }
            expect(took < 15000, `pass ${pass}: stepping back to ${view} took ${(took / 1000).toFixed(1)} s`);
            await assertLive(page, `pass ${pass}: ${view} after stepping back`);
        }
    }

    // A presenter does not wait for one slide to settle before pressing again. Stepping quickly queues a build and
    // a teardown in consecutive frames, which is the condition the paced walk above never creates -- so the keys
    // are sent back to back here, with no wait between them, and the deck has to settle and still be running.
    for (const [direction, key] of [["forwards", kRight], ["backwards", kLeft]]) {
        await walkTo(page, kLeft, kLive[0]);
        const from = await viewOf(page);
        const began = Date.now();
        for (let press = 0; press < 8; ++press) {
            await page.press(...key);
        }
        // Every press is queued and answered a frame at a time, so the cursor keeps travelling after the last key
        // was sent: it has stopped only when it has held still for longer than a graph takes to build.
        let settled = await page.evaluate("(globalThis.gr4Location || '')");
        for (let since = Date.now(); Date.now() - since < 2500; ) {
            const now = await page.evaluate("(globalThis.gr4Location || '')");
            if (now !== settled) {
                settled = now;
                since   = Date.now();
            }
        }
        const took = Date.now() - began;
        expect(settled !== from, `pressing ${direction} eight times without waiting moves the deck, from ${from} to ${String(settled).replace(/^#view=/, "")}`);
        expect(took < 20000, `and it settles in ${(took / 1000).toFixed(1)} s`);
        // where the burst lands is whatever slide the eight presses reach, often one with nothing live on it, so the
        // graphs are checked where they are: on a live slide reached afterwards, which each has to have survived to reach
        // a burst backwards retraces the history, which can reach the deck's start, a long walk from the live slides
        await walkTo(page, direction === "forwards" && (await viewOf(page)) !== "live" ? kLeft : kRight, "live-timing", 120);
        await assertLive(page, `live-timing, reached after eight quick presses ${direction},`);
    }

    // and the deck is still navigable at the end of all that, which a stalled runtime would not be
    const before = await page.evaluate("(globalThis.gr4Location || '')");
    await page.press(...kRight);
    expect(Boolean(await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? (globalThis.gr4Location || '') : false`)), "the deck still answers a key after walking the live slides twice");

    if (failures.length > 0) {
        console.error("console output:\n  " + page.consoleLines.slice(-40).join("\n  "));
    }
    await page.close();
});

console.log(failures.length === 0 ? "qa_BrowserLiveWalk: all checks passed" : `qa_BrowserLiveWalk: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

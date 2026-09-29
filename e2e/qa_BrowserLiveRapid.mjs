// Stepping quickly through the live slides must not leave the deck frozen.
//
// The paced walk in qa_BrowserLiveWalk never reproduced the stall the author reported, because it waits for each
// cursor change before pressing again. A presenter does not. Pressing the key repeatedly moves the cursor faster
// than a graph can be built, so the set of graphs the deck wants to hold changes every frame: one is built and
// another destroyed in the same frame, on the thread drawing it, and destroying one joins a scheduler thread.
//
// What is asserted is what a presenter would see: the deck moved, and it is still alive afterwards -- a picture
// that still changes from frame to frame. A frozen runtime keeps its last frame on the canvas, so only a
// difference between frames can tell the two apart.
//
// Kept separate from the walk because it runs in about two minutes rather than twenty-five, which is what makes
// it usable while changing the thing it tests.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";

const directory = process.argv[2];
if (!directory) {
    console.error("usage: qa_BrowserLiveRapid.mjs <viewer-web directory>");
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

const viewOf = async (page) => (await page.evaluate("(globalThis.gr4Location || '')")).replace(/^#view=/, "").replace(/&.*$/, "");
const frameOf = async (page) => (await page.command("Page.captureScreenshot", { format: "png" })).result.data;

const walkTo = async (page, key, wanted, budget = 40) => {
    for (let step = 0; step < budget; ++step) {
        if ((await viewOf(page)) === wanted) {
            return true;
        }
        const before = await page.evaluate("(globalThis.gr4Location || '')");
        await page.press(...key);
        if (!(await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? (globalThis.gr4Location || '') : false`))) {
            return false;
        }
    }
    return false;
};

/// true when the canvas is still being repainted with something new
const stillRunning = async (page) => {
    // A graph that cannot be restarted is rebuilt after two seconds of trying, so the deck is given longer than
    // that to come back before it is called dead -- which is also about as long as a presenter would wait before
    // deciding something is wrong.
    for (let settle = 0; settle < 90; ++settle) {
        await page.command("Page.captureScreenshot", { format: "png" });
    }
    const first = await frameOf(page);
    for (let attempt = 0; attempt < 20; ++attempt) {
        if ((await frameOf(page)) !== first) {
            return true;
        }
    }
    return false;
};

await withViewer(directory, async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html#view=rejoin&step=0`);
    // A hang throws out of the walk before the checks below can report, so what the page last said, and whether its
    // frames still change, is printed on the way out: a frozen main loop and a deck that only ignores keys need
    // different fixes.
    const explain = async () => {
        // a screenshot comes from the compositor, so it answers even when the page's main thread does not
        const shot = () => Promise.race([frameOf(page), new Promise((resolve) => setTimeout(() => resolve("no screenshot"), 10000))]);
        const first = await shot();
        let moving  = false;
        for (let attempt = 0; attempt < 10 && !moving; ++attempt) {
            moving = (await shot()) !== first;
        }
        console.error(`frames still changing: ${moving}\nconsole output:\n  ` + page.consoleLines.slice(-30).join("\n  "));
    };
    try {
        await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
        // the link's own fragment is there before the deck has loaded, so the deck's published state is waited for
        await page.waitFor("(globalThis.gr4Location || '').includes('view=') && (globalThis.gr4Location || '').includes('step=0') && window.gr4Zoom ? 1 : false");
        await page.evaluate("document.getElementById('canvas').focus()");
        await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: 900, y: 690 });

        for (const [direction, key, from] of [["forwards", kRight, "live"], ["backwards", kLeft, "live-graph"]]) {
            expect(await walkTo(page, kRight, from), `reached ${from} to start from`);
            const before = await viewOf(page);

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
            const after = await viewOf(page);
            expect(after !== before, `eight quick presses ${direction} move the deck, ${before} -> ${after}`);
            // the landing slide is wherever the presses ended, often one with nothing live on it: the graphs are checked
            // on a live slide reached afterwards, which each has to have survived to reach
            // a burst backwards retraces the history, which can reach the deck's start, a long walk from the live slides
            await walkTo(page, direction === "forwards" ? kLeft : kRight, "live-timing", 120);
            expect(await stillRunning(page), `and live-timing, reached after eight quick presses ${direction}, is still drawing`);

            // A picture that stops changing can mean two different things, and they need different fixes: the runtime
            // has stopped answering at all, or it is answering and the chart on the slide has died. One more key
            // press tells them apart.
            const stamp = await page.evaluate("(globalThis.gr4Location || '')");
            await page.press(...key);
            const answered = await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(stamp)} ? (globalThis.gr4Location || '') : false`);
            expect(Boolean(answered), `and it still answers a key afterwards, ${direction} from ${after}`);
        }

    } catch (error) {
        failures.push(String(error));
        console.log(`  FAIL  ${error.message}`);
        await explain();
        await page.close();
        return;
    }
    if (failures.length > 0) {
        await explain();
    }
    // reported whether or not anything failed: a pool that reached its thread limit and queued instead of stopping
    // the page is worth knowing about even when the deck survived it
    const queued = page.consoleLines.filter((line) => line.includes("thread limit reached")).length;
    console.log(`thread-limit warnings: ${queued}`);
    await page.close();
});

console.log(failures.length === 0 ? "qa_BrowserLiveRapid: all checks passed" : `qa_BrowserLiveRapid: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

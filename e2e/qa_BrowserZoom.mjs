// A slide is magnified by the wheel or a pinch, panned by a drag, and brought back by a double click, Escape or
// turning to another slide. The arithmetic is covered by qa_ZoomPan; what this checks is that it reaches the screen.
//
// Ground truth is geometry, measured twice and independently: the wheel is turned with the pointer on the top edge (one pixel in, since a pointer outside the window is no pointer),
// so the point under it does not move and everything else moves away from it by the scale. The scale is then read
// off the picture two ways -- from how much taller the heading's glyphs are, and from how far down the heading has
// gone -- and the two have to agree.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { compareImages, decodePng, pixelAt } from "../devtools/png.mjs";

const directory = process.argv[2];
if (!directory) {
    console.error("usage: qa_BrowserZoom.mjs <viewer-web directory>");
    process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
    console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
    if (!condition) {
        failures.push(description);
    }
};

const kMenuEdge = 0.36; // of the width: clear of the side menu, which is sized in ems
const isLit = (pixel) => pixel.r + pixel.g + pixel.b > 110;
const litRow = (image, y) => {
    for (let x = Math.floor(image.width * kMenuEdge); x < image.width * 0.9; x += 2) {
        if (isLit(pixelAt(image, x, y))) {
            return true;
        }
    }
    return false;
};

/// the first line of ink in the right-hand part of the picture: where it starts and how tall it is
const firstLine = (image) => {
    let top = -1;
    for (let y = 0; y < image.height && top < 0; ++y) {
        if (litRow(image, y)) {
            top = y;
        }
    }
    if (top < 0) {
        return { top: -1, height: -1 };
    }
    let bottom = top;
    while (bottom + 1 < image.height && litRow(image, bottom + 1)) {
        ++bottom;
    }
    return { top, height: bottom - top + 1 };
};

const frameOf = async (page) => decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));
const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const mouse = (page, type, x, y, extra = {}) => page.command("Input.dispatchMouseEvent", { type, x, y, ...extra });
const wheel = async (page, x, y, deltaY, notches) => {
    // the wheel event carries a position but the page's pointer is only moved by motion, and it is that one that counts
    await mouse(page, "mouseMoved", x, y);
    await pause(100);
    for (let notch = 0; notch < notches; ++notch) {
        await mouse(page, "mouseWheel", x, y, { deltaX: 0, deltaY });
        await pause(40);
    }
    await pause(300);
};

await withViewer(directory, async ({ server, browser }) => {
    const page = await openPage(browser, `${server.origin}/index.html?mode=windowed`);
    await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
    await page.waitFor("(globalThis.gr4Location || '').includes('view=') && (globalThis.gr4Location || '').includes('step=0')");
    await page.evaluate("document.getElementById('canvas').focus()");
    for (let settle = 0; settle < 6; ++settle) {
        await mouse(page, "mouseMoved", 900 + settle, 400);
        await pause(100);
    }
    await pause(1000);

    const whole = await frameOf(page);
    const line = firstLine(whole);
    expect(line.top > 0 && line.height > 4, `the untouched slide has a heading to measure, ${line.height} px tall at ${line.top}`);

    // the wheel is turned towards the screen with the pointer on the top edge (one pixel in, since a pointer outside the window is no pointer),
    // above the heading, so the heading stays the first line on screen however far it is magnified
    await wheel(page, 120, 1, -120, 4);
    const zoomed = await frameOf(page);
    const magnified = firstLine(zoomed);
    const byHeight = magnified.height / line.height;
    const byPosition = magnified.top / line.top;
    expect(byHeight > 1.3 && byHeight <= 4.1, `the wheel magnifies the slide, glyphs ${byHeight.toFixed(2)} times taller`);
    expect(Math.abs(byHeight - byPosition) < 0.15 * byHeight, `the heading's height (${byHeight.toFixed(2)}x) and its distance from the pointer's edge (${byPosition.toFixed(2)}x) give the same scale`);

    await page.press("Escape", "Escape", 27);
    await pause(500);
    const afterEscape = compareImages(whole, await frameOf(page));
    expect(afterEscape.comparable && !afterEscape.regression, `Escape restores the untouched slide, largest differing blob ${afterEscape.largestBlob} px`);

    // a drag pans: the slide follows the pointer, up to where its edge would come into view
    await wheel(page, 900, 1, -120, 4);
    const beforePan = firstLine(await frameOf(page));
    await mouse(page, "mouseMoved", 900, 400);
    await mouse(page, "mousePressed", 900, 400, { button: "left", buttons: 1, clickCount: 1 });
    for (let move = 1; move <= 10; ++move) {
        await mouse(page, "mouseMoved", 900, 400 - move * 6, { button: "left", buttons: 1 });
        await pause(40);
    }
    await mouse(page, "mouseReleased", 900, 340, { button: "left", buttons: 0, clickCount: 1 });
    await pause(400);
    const afterPan = firstLine(await frameOf(page));
    const moved = beforePan.top - afterPan.top;
    expect(moved > 40 && moved <= 70, `dragging 60 px upwards moves the slide up by as much, it moved ${moved} px`);

    // a double click brings the whole slide back; the page is windowed, because a click is what lets a browser enter
    // fullscreen, and a headless one answers that by resizing the canvas to its default 800x600
    await mouse(page, "mouseMoved", 900, 400);
    for (let click = 1; click <= 2; ++click) {
        // the page's pointer is put back each time: a synthesised press and release leave it at the origin
        await mouse(page, "mouseMoved", 900, 400);
        await mouse(page, "mousePressed", 900, 400, { button: "left", buttons: 1, clickCount: click });
        await mouse(page, "mouseReleased", 900, 400, { button: "left", buttons: 0, clickCount: click });
        await pause(60);
    }
    await pause(500);
    const afterDouble = compareImages(whole, await frameOf(page));
    expect(afterDouble.comparable && !afterDouble.regression, `a double click restores the untouched slide, largest differing blob ${afterDouble.largestBlob} px`);

    // turning to another slide brings it back too
    await wheel(page, 900, 1, -120, 4);
    const before = await page.evaluate("(globalThis.gr4Location || '')");
    await page.press("ArrowRight", "ArrowRight", 39);
    await page.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? (globalThis.gr4Location || '') : false`);
    await pause(1500);
    expect(await page.evaluate("window.gr4Zoom[0]") === 1, `the next slide is not magnified, the viewer reports ${await page.evaluate("JSON.stringify(window.gr4Zoom)")}`);

    // a pinch magnifies by the ratio of the finger distances: 200 px apart spread to 400 px makes the slide twice as large
    const touch = (type, points) => page.command("Input.dispatchTouchEvent", { type, touchPoints: points.map(([x, y], id) => ({ x, y, id })) });
    await touch("touchStart", [[540, 360], [740, 360]]);
    for (let step = 1; step <= 10; ++step) {
        await touch("touchMove", [[540 - step * 10, 360], [740 + step * 10, 360]]);
        await pause(40);
    }
    await touch("touchEnd", []);
    await pause(400);
    const pinched = await page.evaluate("window.gr4Zoom[0]");
    expect(Math.abs(pinched - 2) < 0.2, `a pinch that doubles the finger distance doubles the slide, the viewer reports ${pinched}x`);

    // F5 and Escape are the browser's: reload, and leave full screen. A cancellable key event sent to the canvas comes
    // back not prevented for them, and prevented for a key the viewer takes, which is what shows the check can fail.
    const prevented = (key, code) => page.evaluate(`!document.getElementById('canvas').dispatchEvent(new KeyboardEvent('keydown', { key: '${key}', code: '${code}', bubbles: true, cancelable: true }))`);
    expect(!(await prevented("F5", "F5")), "F5 reaches the browser, which reloads the page");
    expect(!(await prevented("Escape", "Escape")), "Escape reaches the browser, which leaves full screen");
    expect(await prevented("ArrowRight", "ArrowRight"), "while the viewer keeps an arrow key for itself");

    if (failures.length > 0) {
        console.error("console output:\n  " + page.consoleLines.slice(-30).join("\n  "));
    }
    await page.close();
});

console.log(failures.length === 0 ? "qa_BrowserZoom: all checks passed" : `qa_BrowserZoom: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

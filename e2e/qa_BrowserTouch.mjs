// Advancing a deck with a fingertip exists only in a browser: it needs real touch events, the canvas's `touch-action`
// and OpenDigitizer's finger tracking together, and none of the three can be exercised natively. The recogniser's own
// arithmetic is covered by qa_SwipeNavigation; what is asserted here is that a finger reaches it at all, that a flick
// read once advances exactly once, and that a tap -- which the same code path turns into a left click -- does not.
//
// "Exactly once" and "not at all" are both claims that something did *not* happen, and are asserted without waiting on
// a clock: the cursor one forward step away is measured first with the arrow key, and the gesture then has to land on
// that same cursor and no further. A second advance would show as a different hash, not as a slower one.
//
// Portrait is tested as well as landscape because the swipe threshold is a share of the viewport width, and portrait
// is where that share is smallest -- a flick that carries on a desktop can fall short on a phone held upright.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserTouch.mjs <viewer-web directory>");
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

const kViewports = [
  {
    name: "landscape 1280x720",
    width: 1280,
    height: 720,
    deviceScaleFactor: 1,
  },
  {
    name: "portrait 390x844 at DPR 3",
    width: 390,
    height: 844,
    deviceScaleFactor: 3,
  },
];

/// a cursor that never settles is a failure of this test, not a reason to abandon the assertions after it
const settledAt = async (page, expression) => {
  try {
    return await page.waitFor(expression, 15_000);
  } catch {
    return false;
  }
};

const reaches = (page, hash) =>
  settledAt(
    page,
    `(globalThis.gr4Location || '') === ${JSON.stringify(hash)} ? (globalThis.gr4Location || '') : false`,
  );
const leaves = (page, hash) =>
  settledAt(
    page,
    `(globalThis.gr4Location || '') !== ${JSON.stringify(hash)} ? (globalThis.gr4Location || '') : false`,
  );

await withViewer(directory, async ({ server, browser }) => {
  const page = await openPage(browser, `${server.origin}/index.html`);
  await page.command("Emulation.setTouchEmulationEnabled", {
    enabled: true,
    maxTouchPoints: 5,
  });

  /// a flick: down, a few moves so the browser and SDL see a drag rather than a jump, then up
  const flick = async (fromX, toX, y) => {
    const steps = 8;
    await page.command("Input.dispatchTouchEvent", {
      type: "touchStart",
      touchPoints: [{ x: fromX, y }],
    });
    for (let step = 1; step <= steps; ++step) {
      await page.command("Input.dispatchTouchEvent", {
        type: "touchMove",
        touchPoints: [{ x: fromX + ((toX - fromX) * step) / steps, y }],
      });
    }
    await page.command("Input.dispatchTouchEvent", {
      type: "touchEnd",
      touchPoints: [],
    });
  };

  const tap = async (x, y) => {
    await page.command("Input.dispatchTouchEvent", {
      type: "touchStart",
      touchPoints: [{ x, y }],
    });
    await page.command("Input.dispatchTouchEvent", {
      type: "touchEnd",
      touchPoints: [],
    });
  };

  /// a flick the browser takes away halfway through, which is what happens when the system claims the gesture
  const cancelledFlick = async (fromX, toX, y) => {
    const steps = 8;
    await page.command("Input.dispatchTouchEvent", {
      type: "touchStart",
      touchPoints: [{ x: fromX, y }],
    });
    for (let step = 1; step <= steps; ++step) {
      await page.command("Input.dispatchTouchEvent", {
        type: "touchMove",
        touchPoints: [{ x: fromX + ((toX - fromX) * step) / steps, y }],
      });
    }
    await page.command("Input.dispatchTouchEvent", {
      type: "touchCancel",
      touchPoints: [],
    });
  };

  expect(
    await page.waitFor(
      "(globalThis.gr4Location || '').includes('view=') && (globalThis.gr4Location || '').includes('step=0')",
    ),
    "the viewer publishes its starting cursor once the presentation is loaded",
  );

  for (const viewport of kViewports) {
    console.log(`  -- ${viewport.name}`);
    await page.command("Emulation.setDeviceMetricsOverride", {
      width: viewport.width,
      height: viewport.height,
      deviceScaleFactor: viewport.deviceScaleFactor,
      mobile: true,
    });
    await page.evaluate("document.getElementById('canvas').focus()");

    // the near and far edges of the gesture, kept off the very rim where a browser claims the touch for itself
    const near = Math.round(viewport.width * 0.15);
    const far = Math.round(viewport.width * 0.85);
    const middle = Math.round(viewport.height * 0.5);

    // one forward step, measured with the key, so the gesture has something exact to be compared against
    const start = await page.evaluate("(globalThis.gr4Location || '')");
    await page.press(...kRight);
    const oneStepOn = await leaves(page, start);
    expect(
      Boolean(oneStepOn),
      `a forward key moves the cursor, from ${JSON.stringify(start)} to ${JSON.stringify(oneStepOn)}`,
    );
    await page.press(...kLeft);
    expect(
      Boolean(await reaches(page, start)),
      "a backward key returns where it came from",
    );

    // right to left is the direction a page turns forward, the same way every reader on a phone works
    await flick(far, near, middle);
    expect(
      Boolean(await reaches(page, String(oneStepOn))),
      `a leftward flick advances the deck, to ${JSON.stringify(oneStepOn)}`,
    );

    await flick(near, far, middle);
    expect(
      Boolean(await reaches(page, start)),
      "a rightward flick returns where it came from",
    );

    // the same lift that ends a flick is turned into a left click by the finger tracking, so a tap has to be told
    // apart by its travel; if it advanced too, the forward key below would land a step further on than it should
    await tap(Math.round(viewport.width * 0.5), middle);
    await page.press(...kRight);
    expect(
      Boolean(await reaches(page, String(oneStepOn))),
      "a tap in the middle of the slide does not move the cursor",
    );
    await page.press(...kLeft);
    expect(
      Boolean(await reaches(page, start)),
      "the deck is back at the cursor the gestures started from",
    );

    // a flick whose finger flags were not cleared is read again on the next frame and advances twice, which shows
    // here as a cursor past `oneStepOn` rather than as a slow one
    await flick(far, near, middle);
    expect(
      Boolean(await reaches(page, String(oneStepOn))),
      "one flick moves the cursor exactly once",
    );
    await page.press(...kLeft);
    expect(
      Boolean(await reaches(page, start)),
      "and the deck returns to where it began",
    );

    // A browser sends touchcancel when the system takes the gesture over -- a swipe in from a screen edge, a
    // notification. SDL 3 delivers that as SDL_EVENT_FINGER_CANCELED. A cancelled gesture is not a page turn, and
    // the finger must still be released, or the count never returns to zero and swiping dies for the session.
    await cancelledFlick(far, near, middle);
    await page.press(...kRight);
    expect(
      Boolean(await reaches(page, String(oneStepOn))),
      "a cancelled flick does not turn the page",
    );
    await page.press(...kLeft);
    expect(
      Boolean(await reaches(page, start)),
      "and the deck is back where it was",
    );

    await flick(far, near, middle);
    expect(
      Boolean(await reaches(page, String(oneStepOn))),
      "and a flick after a cancelled one still works",
    );
    await page.press(...kLeft);
    expect(
      Boolean(await reaches(page, start)),
      "so a cancelled touch left no finger behind",
    );
  }

  // A tap has to reach the widgets too, not merely fail to turn the page. Nothing above would notice if taps
  // stopped clicking altogether -- which is how a wrong mouse-button id hid upstream -- so this drives the side
  // menu: a pointer near the left edge reveals it, and a tap on its first entry jumps to that view.
  await page.command("Emulation.setDeviceMetricsOverride", {
    width: 1280,
    height: 720,
    deviceScaleFactor: 1,
    mobile: true,
  });
  const home = await page.evaluate("(globalThis.gr4Location || '')");
  await page.press(...kRight);
  await settledAt(
    page,
    `(globalThis.gr4Location || '') !== ${JSON.stringify(home)} ? (globalThis.gr4Location || '') : false`,
  );

  // the menu opens on pointer proximity and slides in over several frames, so it is revealed and allowed to
  // settle before anything is aimed at it
  await page.command("Input.dispatchMouseEvent", {
    type: "mouseMoved",
    x: 10,
    y: 360,
  });
  let shot = "";
  for (let attempt = 0, stable = 0; attempt < 40 && stable < 3; ++attempt) {
    const next = (
      await page.command("Page.captureScreenshot", { format: "png" })
    ).result.data;
    stable = next === shot ? stable + 1 : 0;
    shot = next;
  }
  // the first slide's entry, wherever the lens has put it
  const [, x, y, w, h] = JSON.parse(
    await page.waitFor(
      "(window.gr4MenuEntries || []).some((entry) => entry[0] === 0) ? JSON.stringify(window.gr4MenuEntries.find((entry) => entry[0] === 0)) : false",
    ),
  );
  await tap(x + w / 2, y + h / 2);
  expect(
    Boolean(await reaches(page, home)),
    "a tap on the side menu's first entry activates it",
  );

  if (failures.length > 0) {
    console.error("console output:\n  " + page.consoleLines.join("\n  "));
  }
  await page.close();

  // A fingertip is wider than a line of small text: on a phone held upright the tour's link is about 15 px tall, and a
  // tap a little below it still means it. Ground truth is the size platforms give a touch target, 44 px (Apple's
  // guidelines, WCAG 2.5.5): a tap within half of that takes the link, one further away does not.
  const phone = await openPage(browser, `${server.origin}/index.html#view=transitions&step=0`);
  await phone.command("Emulation.setDeviceMetricsOverride", { width: 390, height: 844, deviceScaleFactor: 3, mobile: true });
  await phone.command("Emulation.setTouchEmulationEnabled", { enabled: true, maxTouchPoints: 5 });
  await phone.waitFor("(globalThis.gr4Location || '').includes('view=transitions&') && window.gr4Zoom ? 1 : false");
  const tour = JSON.parse(await phone.waitFor("(window.gr4Links || []).some((link) => String(link[4]).startsWith('#tour')) ? JSON.stringify(window.gr4Links.find((link) => String(link[4]).startsWith('#tour'))) : false"));
  const tapPhone = async (x, y) => {
    await phone.command("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: [{ x, y }] });
    await phone.command("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] });
  };
  const middle = [tour[0] + tour[2] / 2, tour[1] + tour[3] / 2];
  await tapPhone(middle[0], middle[1] + 40);
  expect(!(await leaves(phone, "#view=transitions&step=0")), `a tap 40 px below a ${tour[3]} px tall link is not on it`);
  await tapPhone(middle[0], middle[1] + 16);
  expect(Boolean(await leaves(phone, "#view=transitions&step=0")), `a tap 16 px below a ${tour[3]} px tall link takes it, as a fingertip means it, now at ${await phone.evaluate("globalThis.gr4Location")}`);
  await phone.close();
});

process.exit(failures.length === 0 ? 0 : 1);

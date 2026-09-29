// Touch input in a browser: a flick advances the deck exactly once, a tap or cancelled touch does not; the recogniser's
// arithmetic is qa_SwipeNavigation's.
// Ground truth: the cursor one step on, measured first with the arrow key; portrait is included because the swipe
// threshold is a share of the width.

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

const settledAt = async (page, expression) => {
  try {
    return await page.waitFor(expression, 15_000);
  } catch {
    return false;
  }
};

const reaches = (page, hash) => settledAt(page, `(globalThis.gr4Location || '') === ${JSON.stringify(hash)} ? (globalThis.gr4Location || '') : false`);
const leaves = (page, hash) => settledAt(page, `(globalThis.gr4Location || '') !== ${JSON.stringify(hash)} ? (globalThis.gr4Location || '') : false`);

await withViewer(directory, async ({ server, browser }) => {
  const page = await openPage(browser, `${server.origin}/index.html`);
  await page.command("Emulation.setTouchEmulationEnabled", {
    enabled: true,
    maxTouchPoints: 5,
  });

  // a few moves, so the browser and SDL see a drag rather than a jump
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
    await page.waitFor("(globalThis.gr4Location || '').includes('view=') && (globalThis.gr4Location || '').includes('step=0')"),
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

    // kept off the rim, where a browser claims the touch for itself
    const near = Math.round(viewport.width * 0.15);
    const far = Math.round(viewport.width * 0.85);
    const middle = Math.round(viewport.height * 0.5);

    const start = await page.evaluate("(globalThis.gr4Location || '')");
    await page.press(...kRight);
    const oneStepOn = await leaves(page, start);
    expect(Boolean(oneStepOn), `a forward key moves the cursor, from ${JSON.stringify(start)} to ${JSON.stringify(oneStepOn)}`);
    await page.press(...kLeft);
    expect(Boolean(await reaches(page, start)), "a backward key returns where it came from");

    await flick(far, near, middle);
    expect(Boolean(await reaches(page, String(oneStepOn))), `a leftward flick advances the deck, to ${JSON.stringify(oneStepOn)}`);

    await flick(near, far, middle);
    expect(Boolean(await reaches(page, start)), "a rightward flick returns where it came from");

    // the finger tracking turns a lift into a left click, so a tap has to be told apart by its travel
    await tap(Math.round(viewport.width * 0.5), middle);
    await page.press(...kRight);
    expect(Boolean(await reaches(page, String(oneStepOn))), "a tap in the middle of the slide does not move the cursor");
    await page.press(...kLeft);
    expect(Boolean(await reaches(page, start)), "the deck is back at the cursor the gestures started from");

    // uncleared finger flags would be read again on the next frame and advance twice
    await flick(far, near, middle);
    expect(Boolean(await reaches(page, String(oneStepOn))), "one flick moves the cursor exactly once");
    await page.press(...kLeft);
    expect(Boolean(await reaches(page, start)), "and the deck returns to where it began");

    // touchcancel arrives as SDL_EVENT_FINGER_CANCELED; the finger must still be released, or swiping dies for the session
    await cancelledFlick(far, near, middle);
    await page.press(...kRight);
    expect(Boolean(await reaches(page, String(oneStepOn))), "a cancelled flick does not turn the page");
    await page.press(...kLeft);
    expect(Boolean(await reaches(page, start)), "and the deck is back where it was");

    await flick(far, near, middle);
    expect(Boolean(await reaches(page, String(oneStepOn))), "and a flick after a cancelled one still works");
    await page.press(...kLeft);
    expect(Boolean(await reaches(page, start)), "so a cancelled touch left no finger behind");
  }

  // a tap has to reach widgets too, not merely fail to turn the page, so this taps the side menu's first entry
  await page.command("Emulation.setDeviceMetricsOverride", {
    width: 1280,
    height: 720,
    deviceScaleFactor: 1,
    mobile: true,
  });
  const home = await page.evaluate("(globalThis.gr4Location || '')");
  await page.press(...kRight);
  await settledAt(page, `(globalThis.gr4Location || '') !== ${JSON.stringify(home)} ? (globalThis.gr4Location || '') : false`);

  // the menu opens on pointer proximity and slides in over several frames
  await page.command("Input.dispatchMouseEvent", {
    type: "mouseMoved",
    x: 10,
    y: 360,
  });
  let shot = "";
  for (let attempt = 0, stable = 0; attempt < 40 && stable < 3; ++attempt) {
    const next = (await page.command("Page.captureScreenshot", { format: "png" })).result.data;
    stable = next === shot ? stable + 1 : 0;
    shot = next;
  }
  const [, x, y, w, h] = JSON.parse(
    await page.waitFor(
      "(window.gr4MenuEntries || []).some((entry) => entry[0] === 0) ? JSON.stringify(window.gr4MenuEntries.find((entry) => entry[0] === 0)) : false",
    ),
  );
  await tap(x + w / 2, y + h / 2);
  expect(Boolean(await reaches(page, home)), "a tap on the side menu's first entry activates it");

  if (failures.length > 0) {
    console.error("console output:\n  " + page.consoleLines.join("\n  "));
  }
  await page.close();

  // Ground truth: the platform touch target of 44 px (Apple guidelines, WCAG 2.5.5); a tap within half of it takes the link
  const phone = await openPage(browser, `${server.origin}/index.html#view=transitions&step=0`);
  await phone.command("Emulation.setDeviceMetricsOverride", { width: 390, height: 844, deviceScaleFactor: 3, mobile: true });
  await phone.command("Emulation.setTouchEmulationEnabled", { enabled: true, maxTouchPoints: 5 });
  await phone.waitFor("(globalThis.gr4Location || '').includes('view=transitions&') && window.gr4Zoom ? 1 : false");
  const tour = JSON.parse(
    await phone.waitFor(
      "(window.gr4Links || []).some((link) => String(link[4]).startsWith('#tour')) ? JSON.stringify(window.gr4Links.find((link) => String(link[4]).startsWith('#tour'))) : false",
    ),
  );
  const tapPhone = async (x, y) => {
    await phone.command("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: [{ x, y }] });
    await phone.command("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] });
  };
  const middle = [tour[0] + tour[2] / 2, tour[1] + tour[3] / 2];
  await tapPhone(middle[0], middle[1] + 40);
  expect(!(await leaves(phone, "#view=transitions&step=0")), `a tap 40 px below a ${tour[3]} px tall link is not on it`);
  await tapPhone(middle[0], middle[1] + 16);
  expect(
    Boolean(await leaves(phone, "#view=transitions&step=0")),
    `a tap 16 px below a ${tour[3]} px tall link takes it, as a fingertip means it, now at ${await phone.evaluate("globalThis.gr4Location")}`,
  );
  await phone.close();
});

process.exit(failures.length === 0 ? 0 : 1);

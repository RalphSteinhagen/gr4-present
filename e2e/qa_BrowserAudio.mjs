// A clip's Vorbis track is decoded correctly -- qa_VideoStream checks that natively out of the samples -- and
// none of that is audible if the samples never reach a device. That half cannot be asserted natively in CI, and it
// is exactly the half that was broken: the audio subsystem was never started, so a slide asking for sound opened a
// device that could not exist and played silently with nothing said.
//
// In a browser a device is a Web Audio context, so that is what is checked: the application creates one, and it is
// running rather than suspended. AudioContext is patched before the application loads, because a context that has
// already been created cannot be counted afterwards.
//
// A running device is still not a sound. So the graph is tapped as well: every connection to a context's
// destination is routed through an analyser, and the samples passing through it are measured. The frequency the
// samples carry is compared against the band ffmpeg measures in the same file, so the ground truth is a second
// tool's reading and not this test's invention -- a non-zero level at the right frequency is sound, and a
// non-zero level at the wrong one would be a bug that "is it silent?" could not see.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserAudio.mjs <viewer-web directory>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const kTapAudio = `
    window.__ac = [];
    window.__taps = [];
    for (const name of ["AudioContext", "webkitAudioContext"]) {
        const Real = window[name];
        if (!Real) continue;
        window[name] = function (...args) { const made = new Real(...args); window.__ac.push(made); return made; };
        window[name].prototype = Real.prototype;
    }
    // Anything connecting to a destination is routed through an analyser instead, so the samples on their way to
    // the speaker can be read. The original connect is kept and used for both hops, or this recurses for ever.
    const realConnect = AudioNode.prototype.connect;
    AudioNode.prototype.connect = function (target, ...rest) {
        if (target && target.context && target === target.context.destination) {
            const analyser = target.context.createAnalyser();
            analyser.fftSize = 2048;
            window.__taps.push(analyser);
            realConnect.call(analyser, target);
            return realConnect.call(this, analyser, ...rest);
        }
        return realConnect.call(this, target, ...rest);
    };
`;

// the loudest tap, and what frequency it is loudest at; a bin is sampleRate / fftSize wide
const kReadTaps = `
    (() => {
        let best = { rms: 0, hertz: 0 };
        for (const tap of window.__taps || []) {
            const samples = new Float32Array(tap.fftSize);
            tap.getFloatTimeDomainData(samples);
            let sum = 0;
            for (const sample of samples) { sum += sample * sample; }
            const rms = Math.sqrt(sum / samples.length);
            if (rms <= best.rms) continue;
            const spectrum = new Uint8Array(tap.frequencyBinCount);
            tap.getByteFrequencyData(spectrum);
            let peak = 0;
            for (let bin = 1; bin < spectrum.length; ++bin) { if (spectrum[bin] > spectrum[peak]) peak = bin; }
            best = { rms, hertz: (peak * tap.context.sampleRate) / tap.fftSize };
        }
        return JSON.stringify(best);
    })()
`;

await withViewer(directory, async ({ server, browser }) => {
  const page = await openPage(browser, `${server.origin}/index.html`);
  await page.command("Page.enable");
  await page.command("Page.addScriptToEvaluateOnNewDocument", {
    source: kTapAudio,
  });
  await page.evaluate("location.reload()").catch(() => {});
  await page.waitFor("typeof window.__ac !== 'undefined' ? 1 : false");
  await page.waitFor("(globalThis.gr4Location || '').includes('view=')");
  await page.evaluate("document.getElementById('canvas').focus()");

  // walk to the slide that asks for sound, because the device is opened when the clip is first wanted
  for (let step = 0; step < 80; ++step) {
    const view = (await page.evaluate("(globalThis.gr4Location || '')"))
      .replace(/^#view=/, "")
      .replace(/&.*$/, "");
    if (view === "video") {
      break;
    }
    const before = await page.evaluate("(globalThis.gr4Location || '')");
    await page.press("ArrowRight", "ArrowRight", 39);
    await page.waitFor(
      `(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? 1 : false`,
    );
  }
  expect(
    (await page.evaluate("(globalThis.gr4Location || '')")).includes("view=video"),
    "the deck's video slide is reachable",
  );

  // several frames, so the clip is started and its device opened
  for (let frame = 0; frame < 40; ++frame) {
    await page.command("Page.captureScreenshot", { format: "png" });
  }

  const made = await page.evaluate("window.__ac ? window.__ac.length : 0");
  expect(
    made > 0,
    `a slide asking for sound opens an audio device, ${made} created`,
  );

  const states = JSON.parse(
    await page.evaluate(
      "JSON.stringify((window.__ac || []).map((c) => ({ state: c.state, rate: c.sampleRate })))",
    ),
  );
  expect(
    states.some((each) => each.state === "running"),
    `and it is running rather than suspended, ${JSON.stringify(states)}`,
  );
  expect(
    states.length > 0 && states.every((each) => each.rate > 0),
    "at a real sample rate",
  );

  const taps = await page.evaluate("window.__taps ? window.__taps.length : 0");
  expect(taps > 0, `something is connected to that device, ${taps} taps`);

  // The loudest moment over a second or so of frames: a clip that loops clears its queue at the seam, so a single
  // reading can legitimately fall in a gap. What must not happen is that every reading is silent.
  let loudest = { rms: 0, hertz: 0 };
  for (let attempt = 0; attempt < 30; ++attempt) {
    const reading = JSON.parse(await page.evaluate(kReadTaps));
    if (reading.rms > loudest.rms) {
      loudest = reading;
    }
    await page.command("Page.captureScreenshot", { format: "png" });
  }

  // A tenth of full scale is a signal; noise and rounding are orders of magnitude below it.
  expect(
    loudest.rms > 0.01,
    `and sound is reaching it, loudest RMS ${loudest.rms.toFixed(4)}`,
  );
  // The band the clip actually occupies, measured with ffmpeg rather than guessed:
  //   ffprobe -f lavfi "amovie=<clip>,aspectralstats=measure=centroid"
  // over the first eight seconds gives a spectral centroid averaging 1822 Hz and peaking at 2780 Hz -- 1928
  // optical sound, band-limited and tonal. A peak inside 800..3200 Hz is that recording; silence, white noise or
  // a decode that produced something else would not land there, and neither would the 440 Hz test tone this
  // assertion used to name, which is what makes the check still worth making.
  expect(
    loudest.hertz > 800.0 && loudest.hertz < 3200.0,
    `and it is the clip's own sound, peak at ${loudest.hertz.toFixed(1)} Hz inside the 800..3200 Hz its spectrum occupies`,
  );

  // The buttons under the clip: the viewer publishes where each is and whether it is on, and a real click on one
  // has to change that and, for mute, what reaches the device.
  const kButtons = "JSON.stringify(window.gr4VideoButtons || [])";
  const buttonsNow = async () =>
    Object.fromEntries(
      JSON.parse(await page.evaluate(kButtons)).map(([action, x, y, w, h, on]) => [
        ["playPause", "rewind", "mute"][action],
        { x, y, w, h, on: on === 1 },
      ]),
    );
  const clickButton = async (name) => {
    const button = (await buttonsNow())[name];
    const at = { x: button.x + button.w / 2, y: button.y + button.h / 2, button: "left", clickCount: 1 };
    await page.command("Input.dispatchMouseEvent", { type: "mouseMoved", x: at.x, y: at.y });
    await page.command("Input.dispatchMouseEvent", { type: "mousePressed", ...at });
    await page.command("Input.dispatchMouseEvent", { type: "mouseReleased", ...at });
  };
  const loudestNow = async () => {
    let best = 0.0;
    for (let attempt = 0; attempt < 20; ++attempt) {
      best = Math.max(best, JSON.parse(await page.evaluate(kReadTaps)).rms);
      await page.command("Page.captureScreenshot", { format: "png" });
    }
    return best;
  };

  const before = await buttonsNow();
  expect(
    before.playPause && before.rewind && before.mute,
    `a clip with sound has play or pause, rewind and mute, ${Object.keys(before)}`,
  );
  expect(
    before.playPause.x < before.rewind.x && before.rewind.x < before.mute.x,
    "in that order from the left, with mute last",
  );
  expect(before.playPause.on && !before.mute.on, "playing and audible to begin with");

  await clickButton("mute");
  await page.waitFor("JSON.parse(" + kButtons + ").some((b) => b[0] === 2 && b[5] === 1) ? 1 : false");
  expect(true, "a click on mute silences the clip");
  // what the device already held plays out first, so silence is waited for and then has to hold
  await page.waitFor(`JSON.parse(${kReadTaps}).rms < 0.005 ? 1 : false`);
  const silenced = await loudestNow();
  expect(silenced < 0.005, `and once what was buffered has played out nothing reaches the device, loudest RMS ${silenced.toFixed(4)}`);

  await clickButton("mute");
  await page.waitFor("JSON.parse(" + kButtons + ").some((b) => b[0] === 2 && b[5] === 0) ? 1 : false");
  let restored = 0.0;
  for (let round = 0; round < 3 && restored <= 0.01; ++round) {
    restored = await loudestNow();
  }
  expect(restored > 0.01, `a second click brings the sound back, loudest RMS ${restored.toFixed(4)}`);

  // rewind: the clip is somewhere past its start, a click puts it back there and leaves it playing
  const positionNow = async () => Number(await page.evaluate("window.gr4VideoPosition || 0"));
  expect(await page.waitFor("(window.gr4VideoPosition || 0) >= 1.5 ? 1 : false"), "the clip has played on past its first second and a half");
  const beforeRewind = await positionNow();
  await clickButton("rewind");
  const afterRewind = await page.waitFor("(window.gr4VideoPosition || 0) < 1.0 ? window.gr4VideoPosition + 1 : false");
  expect(Boolean(afterRewind), `a click on rewind returns the clip to its start, from ${beforeRewind} s to under 1 s`);
  expect((await buttonsNow()).playPause.on, "and it goes on playing");
  expect(await page.waitFor("(window.gr4VideoPosition || 0) >= 0.5 ? 1 : false"), "forwards from there");

  await clickButton("playPause");
  await page.waitFor("JSON.parse(" + kButtons + ").some((b) => b[0] === 0 && b[5] === 0) ? 1 : false");
  expect(true, "a click on pause stops the clip");
  await clickButton("playPause");
  await page.waitFor("JSON.parse(" + kButtons + ").some((b) => b[0] === 0 && b[5] === 1) ? 1 : false");
  expect(true, "and another starts it again");

  if (failures.length > 0) {
    console.error("console output:\n  " + page.consoleLines.join("\n  "));
  }
  await page.close();
});

process.exit(failures.length === 0 ? 0 : 1);

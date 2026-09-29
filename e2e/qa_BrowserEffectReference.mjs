// A third-party effect drawn by the viewer against the same file drawn by a minimal WebGL 2 page that shares none of
// the viewer's code: its own preamble, its own `main`, one full-screen triangle (D23, D30). Then two analytic effects
// whose pixels follow from the convention alone: a ramp, and a clock fed back through a buffer.
//
// Ground truth is the reference page. The viewer shows `starnest` in a viewport at its still time (`?frozen`: time 0,
// no pointer, scale 1); the page renders the file at the viewport's size with `iTime` 0 and `iMouse` 0. Where the
// viewport sits is measured, not assumed: the same box first shows a solid magenta effect. Tolerance (D30): mean
// absolute error <= 2/255 and maximum <= 8/255 per channel.

import { openPage, withViewer } from "../devtools/browser_harness.mjs";
import { decodePng } from "../devtools/png.mjs";
import { cpSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserEffectReference.mjs <viewer-web directory>");
  process.exit(2);
}

const failures = [];
const expect = (condition, description) => {
  console.log(`  ${condition ? "ok  " : "FAIL"}  ${description}`);
  if (!condition) {
    failures.push(description);
  }
};

const kWidth = 1280;
const kHeight = 720;

const shot = async (page) => decodePng(Buffer.from((await page.command("Page.captureScreenshot", { format: "png" })).result.data, "base64"));

const deckWith = (effect, extra = "") => `# Reference {#reference}

:::place
screen: 0.25 0.3 0.5 0.5
:::

:::shader
id: under-test
region: screen
effect: ${effect}
${extra}:::
`;

const kMagenta = "// @name magenta\nvoid mainImage(out vec4 colour, in vec2 fragCoord) { colour = vec4(1.0, 0.0, 1.0, 1.0); }\n";

// the reference page: nothing of the viewer but the effect file it fetches
const kReferencePage = `<!doctype html><html><body style="margin:0;background:#000">
<canvas id="c"></canvas>
<script>
globalThis.render = async (width, height) => {
  const canvas = document.getElementById("c");
  canvas.width = width;
  canvas.height = height;
  canvas.style.width = width + "px";
  canvas.style.height = height + "px";
  const gl = canvas.getContext("webgl2", { antialias: false, preserveDrawingBuffer: true });
  const effect = await (await fetch("default/effects/starnest.glsl")).text();
  const fragment = "#version 300 es\\nprecision highp float;\\nprecision highp int;\\n" +
    "uniform vec3 iResolution; uniform float iTime; uniform vec4 iMouse; uniform int iFrame;\\nout vec4 outColour;\\n" +
    effect + "\\nvoid main() { vec4 c = vec4(0.0); mainImage(c, gl_FragCoord.xy); outColour = vec4(c.rgb, 1.0); }\\n";
  const vertex = "#version 300 es\\nvoid main() { vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0); gl_Position = vec4(p, 0.0, 1.0); }\\n";
  const compiled = (type, text) => { const s = gl.createShader(type); gl.shaderSource(s, text); gl.compileShader(s); if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s)); return s; };
  const program = gl.createProgram();
  gl.attachShader(program, compiled(gl.VERTEX_SHADER, vertex));
  gl.attachShader(program, compiled(gl.FRAGMENT_SHADER, fragment));
  gl.linkProgram(program);
  gl.useProgram(program);
  gl.uniform3f(gl.getUniformLocation(program, "iResolution"), width, height, 1.0);
  gl.uniform1f(gl.getUniformLocation(program, "iTime"), 0.0);
  gl.uniform4f(gl.getUniformLocation(program, "iMouse"), 0.0, 0.0, 0.0, 0.0);
  gl.uniform1i(gl.getUniformLocation(program, "iFrame"), 0);
  gl.viewport(0, 0, width, height);
  gl.bindVertexArray(gl.createVertexArray());
  gl.drawArrays(gl.TRIANGLES, 0, 3);
  gl.finish();
  return 1;
};
</script></body></html>`;

/// the rectangle of exactly-magenta pixels, or null
const magentaBox = (image) => {
  let left = image.width, top = image.height, right = -1, bottom = -1;
  for (let y = 0; y < image.height; ++y) {
    for (let x = 0; x < image.width; ++x) {
      const i = (y * image.width + x) * 4;
      if (image.rgba[i] === 255 && image.rgba[i + 1] === 0 && image.rgba[i + 2] === 255) {
        left = Math.min(left, x);
        top = Math.min(top, y);
        right = Math.max(right, x);
        bottom = Math.max(bottom, y);
      }
    }
  }
  return right < 0 ? null : { x: left, y: top, width: right - left + 1, height: bottom - top + 1 };
};

const settled = async (browser, server, query) => {
  const page = await openPage(browser, `${server.origin}/index.html${query}#view=reference&step=0`);
  await page.command("Emulation.setDeviceMetricsOverride", { width: kWidth, height: kHeight, deviceScaleFactor: 1, mobile: false });
  await page.waitFor("(globalThis.gr4Location || '').includes('view=reference') && window.gr4Zoom ? 1 : false");
  let image = null;
  for (let frame = 0; frame < 30; ++frame) {
    image = await shot(page);
  }
  await page.close();
  return image;
};

const copy = mkdtempSync(join(tmpdir(), "effect-reference-"));
try {
  cpSync(directory, copy, { recursive: true });
  const deck = join(copy, "default", "talk.md");
  writeFileSync(join(copy, "default", "effects", "magenta.glsl"), kMagenta);
  writeFileSync(join(copy, "reference.html"), kReferencePage);

  writeFileSync(deck, deckWith("magenta"));
  let box = null;
  await withViewer(copy, async ({ server, browser }) => {
    box = magentaBox(await settled(browser, server, "?frozen"));
  });
  expect(box !== null && box.width > 100 && box.height > 100, `the viewport is found: ${JSON.stringify(box)}`);

  writeFileSync(deck, deckWith("starnest"));
  await withViewer(copy, async ({ server, browser }) => {
    const viewer = await settled(browser, server, "?frozen");
    const page = await openPage(browser, `${server.origin}/reference.html`);
    await page.command("Emulation.setDeviceMetricsOverride", { width: kWidth, height: kHeight, deviceScaleFactor: 1, mobile: false });
    await page.waitFor("typeof globalThis.render === 'function' ? 1 : false");
    await page.evaluate(`globalThis.render(${box.width}, ${box.height})`);
    const reference = await shot(page);
    await page.close();

    const channels = [0, 0, 0];
    const worst = [0, 0, 0];
    for (let y = 0; y < box.height; ++y) {
      for (let x = 0; x < box.width; ++x) {
        const v = ((box.y + y) * viewer.width + box.x + x) * 4;
        const r = (y * reference.width + x) * 4;
        for (let c = 0; c < 3; ++c) {
          const d = Math.abs(viewer.rgba[v + c] - reference.rgba[r + c]);
          channels[c] += d;
          worst[c] = Math.max(worst[c], d);
        }
      }
    }
    const count = box.width * box.height;
    const mean = channels.map((sum) => sum / count);
    expect(mean.every((m) => m <= 2), `mean absolute error per channel <= 2/255: ${mean.map((m) => m.toFixed(3)).join(", ")}`);
    expect(worst.every((w) => w <= 8), `largest error per channel <= 8/255: ${worst.join(", ")}`);
  });

  // analytic effects, whose every pixel follows from the convention alone (D23): float to 8 bits rounds to nearest,
  // which GL ES lets an implementation miss by one level
  writeFileSync(join(copy, "default", "effects", "ramp.glsl"), "void mainImage(out vec4 colour, in vec2 fragCoord) { colour = vec4(fragCoord / iResolution.xy, 0.0, 1.0); }\n");
  // Buffer A counts its renders; the image shows that count, iFrame and iTime. At `@still 0.5` a frozen page renders
  // round(0.5 * 60) = 30 frames after the first, at dt 1/60 from cleared buffers (D25): 31 renders, iFrame 30, iTime 0.5.
  writeFileSync(join(copy, "default", "effects", "clock.glsl"), [
    "// @still 0.5",
    "//--- pass: BufferA",
    "// @channel0 BufferA",
    "void mainImage(out vec4 colour, in vec2 fragCoord) { colour = vec4(texelFetch(iChannel0, ivec2(0), 0).r + 1.0 / 255.0, 0.0, 0.0, 1.0); }",
    "//--- pass: Image",
    "// @channel0 BufferA",
    "void mainImage(out vec4 colour, in vec2 fragCoord) { colour = vec4(texelFetch(iChannel0, ivec2(0), 0).r, float(iFrame) / 255.0, iTime * 0.5, 1.0); }",
    "",
  ].join("\n"));
  const at = (image, x, y) => [0, 1, 2].map((c) => image.rgba[(y * image.width + x) * 4 + c]);
  writeFileSync(deck, deckWith("ramp"));
  await withViewer(copy, async ({ server, browser }) => {
    const viewer = await settled(browser, server, "?frozen");
    let worst = 0;
    for (let y = 0; y < box.height; y += 7) {
      for (let x = 0; x < box.width; x += 7) {
        const [r, g, b] = at(viewer, box.x + x, box.y + y);
        const wantR = Math.round((255 * (x + 0.5)) / box.width);
        const wantG = Math.round((255 * (box.height - 1 - y + 0.5)) / box.height); // GL counts rows from the bottom
        worst = Math.max(worst, Math.abs(r - wantR), Math.abs(g - wantG), b);
      }
    }
    expect(worst <= 1, `ramp: every pixel is fragCoord / iResolution, at most ${worst} level off`);
  });
  writeFileSync(deck, deckWith("clock"));
  await withViewer(copy, async ({ server, browser }) => {
    const viewer = await settled(browser, server, "?frozen");
    const [renders, frame, time] = at(viewer, box.x + (box.width >> 1), box.y + (box.height >> 1));
    expect(Math.abs(renders - 31) <= 1, `clock: Buffer A rendered 31 times from cleared buffers: ${renders}`);
    expect(Math.abs(frame - 30) <= 1, `clock: the last frame is iFrame 30: ${frame}`);
    expect(Math.abs(time - 64) <= 1, `clock: at iTime 0.5 s, round(0.25 * 255) = 64: ${time}`);
  });

  // `aspect:` keeps its shape, as large as the region allows, centred (FR-12)
  writeFileSync(deck, deckWith("magenta", "aspect: 2:1\n"));
  await withViewer(copy, async ({ server, browser }) => {
    const shaped = magentaBox(await settled(browser, server, "?frozen"));
    const ratio = shaped ? shaped.width / shaped.height : 0;
    expect(shaped !== null && Math.abs(ratio - 2) < 0.02 && shaped.height <= box.height && Math.abs(shaped.x + shaped.width / 2 - (box.x + box.width / 2)) <= 1, `aspect 2:1: ${JSON.stringify(shaped)}, ${ratio.toFixed(3)}`);
  });

  // an effect that cannot be drawn shows its `fallback:` still, where without one the box is only a placeholder
  const still = "media/Solvay_conference_1927.webp"; // a bright photograph, unlike the dark placeholder
  let placeholder = null;
  writeFileSync(deck, deckWith("nosuch"));
  await withViewer(copy, async ({ server, browser }) => {
    placeholder = await settled(browser, server, "?frozen");
  });
  writeFileSync(deck, deckWith("nosuch", `fallback: ${still}\n`));
  await withViewer(copy, async ({ server, browser }) => {
    const shown = await settled(browser, server, "?frozen");
    let changed = 0;
    for (let y = box.y; y < box.y + box.height; y += 4) {
      for (let x = box.x; x < box.x + box.width; x += 4) {
        const i = (y * shown.width + x) * 4;
        changed += Math.abs(shown.rgba[i] - placeholder.rgba[i]) + Math.abs(shown.rgba[i + 2] - placeholder.rgba[i + 2]) > 24 ? 1 : 0;
      }
    }
    const share = changed / ((box.width / 4) * (box.height / 4));
    expect(share > 0.3, `an unknown effect shows its fallback still: ${(100 * share).toFixed(1)} % of the box differs from the placeholder`);
  });
} finally {
  rmSync(copy, { recursive: true, force: true });
}

console.log(failures.length === 0 ? "qa_BrowserEffectReference: all checks passed" : `qa_BrowserEffectReference: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

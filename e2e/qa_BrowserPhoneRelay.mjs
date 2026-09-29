// A phone follows the deck through the development server: `serve.mjs --relay` relays the slide between every page
// opened with `?relay=<token>`. The phone is a second browser here, so the two pages share no BroadcastChannel and
// can only meet through the server. Either may lead; a wrong token is refused.

import { launchBrowser, openPage, withViewer } from "../devtools/browser_harness.mjs";
import { networkInterfaces } from "node:os";

// polls time out after half a second here, so the test reaches what a talk reaches after twenty: a move arriving
// after polls have timed out, which once answered a poll twice and brought the server down
process.env.GR4_PRESENT_RELAY_WAIT_MS = "500";

const directory = process.argv[2];
if (!directory) {
  console.error("usage: qa_BrowserPhoneRelay.mjs <viewer-web directory>");
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

const open = async (browser, url) => {
  const page = await openPage(browser, url);
  await page.command("Emulation.setDeviceMetricsOverride", { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
  await page.waitFor("document.getElementById('canvas') && window.gr4Zoom && (globalThis.gr4Location || '').includes('view=') ? 1 : false");
  await page.evaluate("document.getElementById('canvas').focus()");
  return page;
};

await withViewer(
  directory,
  async ({ server, browser }) => {
    const token = /relay token: ([0-9a-f]+)/.exec(server.output())?.[1];
    expect(Boolean(token), "the server announces a relay token");
    if (!token) return;

    // this machine asking by its network address is still this machine, so it is given the token too
    const networkAddress = Object.values(networkInterfaces()).flat().find((address) => address?.family === "IPv4" && !address.internal)?.address;
    if (networkAddress) {
      const port = new URL(server.origin).port;
      const answer = await fetch(`http://${networkAddress}:${port}/relay-link`).then((response) => (response.ok ? response.json() : null)).catch(() => null);
      expect(answer?.token === token, `asked by its own network address ${networkAddress}, the server gives this machine the token`);
    } else {
      console.log("  skip  no network address on this machine to ask by");
    }
    const phoneBrowser = await launchBrowser();
    try {
      const audience = await open(browser, `${server.origin}/index.html?relay=${token}`);
      const phone = await open(phoneBrowser, `${server.origin}/index.html?presenter&relay=${token}`);

      for (let press = 0; press < 2; ++press) {
        const before = await audience.evaluate("(globalThis.gr4Location || '')");
        await audience.press(...kRight);
        await audience.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? 1 : false`);
      }
      const led = await audience.evaluate("(globalThis.gr4Location || '')");
      expect(Boolean(await phone.waitFor(`(globalThis.gr4Location || '') === ${JSON.stringify(led)} ? 1 : false`)), `the phone follows the audience to ${led}`);

      // idle for longer than a poll waits, then let the phone lead
      for (let frame = 0; frame < 20; ++frame) await phone.command("Page.captureScreenshot", { format: "png" });
      await phone.press(...kLeft);
      await phone.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(led)} ? 1 : false`);
      const back = await phone.evaluate("(globalThis.gr4Location || '')");
      expect(Boolean(await audience.waitFor(`(globalThis.gr4Location || '') === ${JSON.stringify(back)} ? 1 : false`)), `and the audience follows the phone back to ${back}`);

      const refused = await audience.evaluate(`fetch("relay/wrong-token?after=0").then((response) => response.status)`);
      expect(refused === 403, `a wrong token is refused, ${refused}`);
      await phone.close();
      await audience.close();

      // Opened on the presenter's machine without a token, the deck asks the server for it and for the phone's
      // link, and shows that link as a code in the first slide's notes, and nowhere else.
      const laptop = await open(browser, `${server.origin}/index.html`);
      const link = await laptop.waitFor("globalThis.gr4PhoneLink ? globalThis.gr4PhoneLink : false");
      expect(typeof link === "string" && link.includes(`?presenter&relay=${token}`), `the laptop's deck learns the phone's link, ${link}`);
      // the relay still holds where the earlier pages went, so this one follows there first; Home goes to the first slide
      await laptop.waitFor("(globalThis.gr4Location || '').includes('view=markdown') ? 1 : false");
      await laptop.press("Home", "Home", 36);
      await laptop.waitFor("(globalThis.gr4Location || '').includes('view=intro') ? 1 : false");
      await laptop.press("n", "KeyN", 78);
      expect(Boolean(await laptop.waitFor("window.gr4PhoneCode === true ? 1 : false")), "the first slide's notes show it as a code");
      const before = await laptop.evaluate("(globalThis.gr4Location || '')");
      await laptop.press(...kRight);
      await laptop.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(before)} ? 1 : false`);
      for (let frame = 0; frame < 10; ++frame) await laptop.command("Page.captureScreenshot", { format: "png" });
      expect((await laptop.evaluate("window.gr4PhoneCode")) === false, "and the second slide's notes do not");
      // the phone opens what the code says, here on this machine's own address
      const phoneLink = new URL(link);
      const handheld = await open(phoneBrowser, `${server.origin}${phoneLink.pathname}${phoneLink.search}`);
      const now = await laptop.evaluate("(globalThis.gr4Location || '')");
      await laptop.press(...kRight);
      await laptop.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(now)} ? 1 : false`);
      const moved = await laptop.evaluate("(globalThis.gr4Location || '')");
      expect(Boolean(await handheld.waitFor(`(globalThis.gr4Location || '') === ${JSON.stringify(moved)} ? 1 : false`)), `a phone that opened that link follows the laptop to ${moved}`);
      for (let frame = 0; frame < 20; ++frame) await handheld.command("Page.captureScreenshot", { format: "png" });
      await handheld.press(...kRight);
      await handheld.waitFor(`(globalThis.gr4Location || '') !== ${JSON.stringify(moved)} ? 1 : false`);
      const phoneLed = await handheld.evaluate("(globalThis.gr4Location || '')");
      expect(Boolean(await laptop.waitFor(`(globalThis.gr4Location || '') === ${JSON.stringify(phoneLed)} ? 1 : false`)), `and the laptop follows that phone to ${phoneLed}`);
      await handheld.close();
      await laptop.close();
    } finally {
      await phoneBrowser.stop();
    }
  },
  { serverArguments: ["--relay", "--host", "0.0.0.0"] },
);

console.log(failures.length === 0 ? "qa_BrowserPhoneRelay: all checks passed" : `qa_BrowserPhoneRelay: ${failures.length} failed`);
process.exit(failures.length === 0 ? 0 : 1);

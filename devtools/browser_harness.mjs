// Drives a headless Chromium over the DevTools protocol so the WASM viewer can be asserted on in a real browser.
// The DOM half of the viewer cannot be tested by the ImGui test engine: what is asserted here is the browser's own
// layout, compositing and event routing.
//
// Serving goes through devtools/serve.mjs, which already sets the COOP/COEP headers the threaded runtime needs.
// Waiting is always for a named condition with a deadline, never for a fixed duration, so a slow machine makes the
// test slower rather than flaky.

import { spawn } from "node:child_process";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";

const kBrowserCandidates = ["google-chrome", "chromium", "chromium-browser", "google-chrome-stable"];
const kDefaultTimeoutMs = 60_000;

class Deadline {
    constructor(milliseconds, what) {
        this.expiry = Date.now() + milliseconds;
        this.what = what;
    }

    check() {
        if (Date.now() > this.expiry) {
            throw new Error(`timed out waiting for ${this.what}`);
        }
    }
}

const untilReachable = async (probe, what, timeoutMs = kDefaultTimeoutMs) => {
    const deadline = new Deadline(timeoutMs, what);
    for (;;) {
        try {
            const value = await probe();
            if (value) {
                return value;
            }
        } catch {
            // not up yet
        }
        deadline.check();
        await new Promise((resolve) => setTimeout(resolve, 50));
    }
};

export const startServer = async (directory, serverArguments = []) => {
    const server = spawn(process.execPath, [path.join(import.meta.dirname, "serve.mjs"), "--no-browser", "--port", "0", "--directory", directory, ...serverArguments], { stdio: ["ignore", "pipe", "inherit"] });
    let output = "";
    const port = await new Promise((resolve, reject) => {
        server.stdout.on("data", (chunk) => {
            output += chunk;
            const match = /listening on port (\d+)/.exec(output);
            if (match) {
                resolve(Number(match[1]));
            }
        });
        server.on("exit", (code) => reject(new Error(`serve.mjs exited with ${code} before reporting a port`)));
    });
    server.stdout.on("data", (chunk) => {
        output += chunk; // kept, so a test can read what the server announced after it started
    });
    return { origin: `http://localhost:${port}`, output: () => output, stop: () => server.kill() };
};

let browsersLaunched = 0;
export const launchBrowser = async (browserArguments = []) => {
    const profile = await mkdtemp(path.join(tmpdir(), "gr4-present-browser-"));
    // one port per test process, so parallel ctest jobs do not collide, and per browser, for a test that needs two
    const port = 9300 + ((process.pid + 101 * browsersLaunched++) % 500);
    let browser = null;
    for (const candidate of kBrowserCandidates) {
        browser = spawn(candidate, ["--headless=new", "--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--no-first-run", "--no-default-browser-check", "--window-size=1280,800", `--user-data-dir=${profile}`, `--remote-debugging-port=${port}`, ...browserArguments, "about:blank"], { stdio: "ignore" });
        const started = await new Promise((resolve) => {
            browser.once("error", () => resolve(false));
            browser.once("spawn", () => resolve(true));
        });
        if (started) {
            break;
        }
        browser = null;
    }
    if (!browser) {
        throw new Error(`no headless browser found, tried: ${kBrowserCandidates.join(", ")}`);
    }
    const endpoint = `http://127.0.0.1:${port}`;
    await untilReachable(() => fetch(`${endpoint}/json/version`).then((response) => response.ok), "the browser's debugging endpoint");
    return {
        endpoint,
        stop: async () => {
            browser.kill("SIGKILL");
            await new Promise((resolve) => browser.once("exit", resolve));
            // the browser is still flushing its cache as it dies, so a single rmdir races it
            await rm(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 });
        },
    };
};

export const openPage = async (browser, url) => {
    const target = await (await fetch(`${browser.endpoint}/json/new?${encodeURIComponent(url)}`, { method: "PUT" })).json();
    const socket = new WebSocket(target.webSocketDebuggerUrl);
    const pending = new Map();
    const consoleLines = [];
    let nextId = 0;

    socket.onmessage = (event) => {
        const message = JSON.parse(event.data);
        if (message.id && pending.has(message.id)) {
            pending.get(message.id)(message);
            pending.delete(message.id);
        }
        if (message.method === "Runtime.consoleAPICalled") {
            consoleLines.push(`${message.params.type}: ${message.params.args.map((a) => a.value ?? a.description ?? "").join(" ")}`);
        }
    };
    await new Promise((resolve) => (socket.onopen = resolve));

    const command = (method, params = {}) =>
        new Promise((resolve) => {
            const id = ++nextId;
            pending.set(id, resolve);
            socket.send(JSON.stringify({ id, method, params }));
        });
    await command("Runtime.enable");

    // bounded, because a page whose main thread is blocked never answers, and a test waiting on it would wait for ever
    // rather than say what it saw
    const kAnswerWithinMs = 60000;
    const evaluate = async (expression) => {
        const message = await Promise.race([
            command("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true }),
            new Promise((_, reject) => setTimeout(() => reject(new Error(`the page did not answer within ${kAnswerWithinMs / 1000} s: ${expression}`)), kAnswerWithinMs)),
        ]);
        const thrown = message.result?.exceptionDetails;
        if (thrown) {
            throw new Error(`evaluating ${expression}: ${thrown.exception?.description ?? thrown.text}`);
        }
        return message.result?.result?.value;
    };

    return {
        evaluate,
        // a raw DevTools call, for what no helper wraps: viewport and device-pixel-ratio overrides, touch
        // emulation, and synthesised touch events
        command,
        consoleLines,
        waitFor: (expression, timeoutMs = kDefaultTimeoutMs) => untilReachable(() => evaluate(expression), expression, timeoutMs),
        /// sends a real key event to the page, as a presentation remote or a keyboard would
        press: async (key, code, windowsVirtualKeyCode) => {
            for (const type of ["rawKeyDown", "keyUp"]) {
                await command("Input.dispatchKeyEvent", { type, key, code, windowsVirtualKeyCode, nativeVirtualKeyCode: windowsVirtualKeyCode });
            }
        },
        screenshot: async (file) => {
            const shot = await command("Page.captureScreenshot", { format: "png" });
            await writeFile(file, Buffer.from(shot.result.data, "base64"));
        },
        close: () => fetch(`${browser.endpoint}/json/close/${target.id}`),
    };
};

/// runs `body` against a freshly served copy of `directory`, tearing both down whatever happens
export const withViewer = async (directory, body, { serverArguments = [], browserArguments = [] } = {}) => {
    const server = await startServer(directory, serverArguments);
    const browser = await launchBrowser(browserArguments);
    try {
        return await body({ server, browser });
    } finally {
        await browser.stop();
        server.stop();
    }
};

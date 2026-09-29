#!/usr/bin/env node
// Serve the Emscripten build of the viewer.
//
// The threaded GR4/OpenDigitizer WASM runtime needs SharedArrayBuffer, which browsers expose only to cross-origin
// isolated pages: that needs COOP and COEP response headers, which an ordinary static file server does not send, so
// the presentation would silently fall back to a single-threaded runtime or fail outright.
//
// SharedArrayBuffer also needs a secure context, and only localhost is exempt. Serving to another machine over plain
// HTTP therefore leaves the runtime unable to start, which is what --https is for.
//
// Node rather than Python because the browser tests already require it, so this adds no dependency.

import { randomBytes } from "node:crypto";
import { createServer as createHttpServer } from "node:http";
import { createServer as createHttpsServer } from "node:https";
import { execFileSync } from "node:child_process";
import { createReadStream, existsSync, readFileSync, statSync } from "node:fs";
import { networkInterfaces } from "node:os";
import path from "node:path";

const kDefaultPort = 8000;
const kDefaultPage = "index.html";

const kContentTypes = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript",
    ".mjs": "text/javascript",
    ".wasm": "application/wasm",
    ".json": "application/json",
    ".yml": "text/yaml; charset=utf-8",
    ".yaml": "text/yaml; charset=utf-8",
    ".md": "text/markdown; charset=utf-8",
    ".css": "text/css",
    ".svg": "image/svg+xml",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".gif": "image/gif",
    ".webp": "image/webp",
    ".ogv": "video/ogg",
    ".oga": "audio/ogg",
    ".ttf": "font/ttf",
    ".otf": "font/otf",
    ".data": "application/octet-stream",
    ".grc": "text/yaml; charset=utf-8",
};

const usage = `usage: serve.mjs [options]

  -d, --directory <dir>  directory holding index.html (default: cmake-build-WASM-Release/viewer-web)
  -p, --port <n>         TCP port, 0 picks a free one (default: ${kDefaultPort})
      --host <address>   address to bind, 0.0.0.0 serves the local network (default: 127.0.0.1)
      --https            serve over TLS with a self-signed certificate
      --page <name>      page to open (default: ${kDefaultPage})
      --no-browser       serve only, do not open a browser
      --relay            relay the slide between this deck's windows, a phone included, under a random token
  -h, --help             show this
`;

const options = { directory: "cmake-build-WASM-Release/viewer-web", port: kDefaultPort, host: "127.0.0.1", https: false, page: kDefaultPage, browser: true, relay: false };
for (let index = 2; index < process.argv.length; ++index) {
    const argument = process.argv[index];
    const next = () => process.argv[++index];
    if (argument === "-d" || argument === "--directory") options.directory = next();
    else if (argument === "-p" || argument === "--port") options.port = Number(next());
    else if (argument === "--host") options.host = next();
    else if (argument === "--https") options.https = true;
    else if (argument === "--page") options.page = next();
    else if (argument === "--no-browser") options.browser = false;
    else if (argument === "--relay") options.relay = true;
    else if (argument === "-h" || argument === "--help") {
        process.stdout.write(usage);
        process.exit(0);
    } else {
        process.stderr.write(`unknown option: ${argument}\n${usage}`);
        process.exit(2);
    }
}

const root = path.resolve(options.directory);
if (!existsSync(root) || !statSync(root).isDirectory()) {
    process.stderr.write(`no such directory: ${root}\nbuild the Emscripten target first, e.g.\n  cmake --preset WASM-Release && cmake --build cmake-build-WASM-Release\n`);
    process.exit(1);
}
if (!existsSync(path.join(root, options.page))) {
    process.stderr.write(`${path.join(root, options.page)} is missing -- has the Emscripten target been built?\n`);
    process.exit(1);
}

/// best guess at this machine's address on the local network
/// every address this machine answers to, loopback included: a request from one of them comes from this machine,
/// whether the page was opened as localhost or by the machine's network address
const ownAddresses = () => {
    const own = new Set(["127.0.0.1", "::1", "::ffff:127.0.0.1"]);
    for (const addresses of Object.values(networkInterfaces())) {
        for (const address of addresses ?? []) {
            own.add(address.address);
            if (address.family === "IPv4") own.add(`::ffff:${address.address}`);
        }
    }
    return own;
};

const localAddress = () => {
    for (const addresses of Object.values(networkInterfaces())) {
        for (const address of addresses ?? []) {
            if (address.family === "IPv4" && !address.internal) {
                return address.address;
            }
        }
    }
    return "127.0.0.1";
};

/// generated once and deliberately throw-away; browsers warn about it, which has to be accepted per viewing machine
const selfSignedCertificate = () => {
    const certificate = path.join(root, "gr4-present-dev-cert.pem");
    const key = path.join(root, "gr4-present-dev-key.pem");
    if (!existsSync(certificate) || !existsSync(key)) {
        execFileSync("openssl", ["req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "365", "-keyout", key, "-out", certificate, "-subj", "/CN=gr4-present development server", "-addext", `subjectAltName=DNS:localhost,IP:127.0.0.1,IP:${localAddress()}`], { stdio: "ignore" });
    }
    return { cert: readFileSync(certificate), key: readFileSync(key) };
};

// The relay: every page opened with `?relay=<token>` posts where it moved and long-polls for where the others moved.
// Opt-in, one random token per run, and nothing else is reachable through it, since anyone on the network who can
// reach the port could otherwise turn the presenter's slides.
const relayToken = options.relay ? randomBytes(16).toString("hex") : "";
const relay = { seq: 0, message: "", waiting: [] };
const kRelayWaitMs = Number(process.env.GR4_PRESENT_RELAY_WAIT_MS ?? 20000); // a test shortens it to reach the timeout
const answerRelay = (response) => {
    if (response.headersSent || response.writableEnded) {
        return; // a poll is answered once, whichever of the timeout, a move or a closed connection comes first
    }
    response.writeHead(200, { "Content-Type": "application/json", "Cache-Control": "no-store", "Cross-Origin-Resource-Policy": "same-origin" }).end(JSON.stringify({ seq: relay.seq, message: relay.message }));
};
const handleRelay = (request, response, token) => {
    if (!relayToken || token !== relayToken) {
        response.writeHead(403).end("forbidden");
        return;
    }
    if (request.method === "POST") {
        let body = "";
        request.on("data", (chunk) => {
            body += chunk;
            if (body.length > 1024) request.destroy(); // a cursor is a few dozen bytes
        });
        request.on("end", () => {
            relay.seq += 1;
            relay.message = body;
            for (const waiter of relay.waiting.splice(0)) {
                clearTimeout(waiter.timer);
                answerRelay(waiter.response);
            }
            response.writeHead(204).end();
        });
        return;
    }
    const after = Number(new URL(request.url, "http://placeholder").searchParams.get("after") ?? "0");
    if (relay.seq > after) {
        answerRelay(response);
        return;
    }
    // a poll leaves the waiting list however it ends, or a later move would answer it a second time
    const waiter = { response, timer: null };
    const done = () => {
        clearTimeout(waiter.timer);
        relay.waiting = relay.waiting.filter((other) => other !== waiter);
    };
    waiter.timer = setTimeout(() => {
        done();
        answerRelay(response);
    }, kRelayWaitMs);
    relay.waiting.push(waiter);
    request.on("close", done);
};

const handle = (request, response) => {
    const requested = decodeURIComponent(new URL(request.url, "http://placeholder").pathname);
    // the presenter's own machine may ask for the token and the phone's link, by any of its addresses, so a deck opened
    // there finds the relay by itself; anyone else on the network gets nothing, which is what the token is for
    if (requested === "/relay-link") {
        const local = ownAddresses().has(request.socket.remoteAddress);
        if (!relayToken || !local) {
            response.writeHead(404).end("not found");
            return;
        }
        const port = server.address().port;
        const phone = `${options.https ? "https" : "http"}://${localAddress()}:${port}/${options.page}?presenter&relay=${relayToken}`;
        response.writeHead(200, { "Content-Type": "application/json", "Cache-Control": "no-store", "Cross-Origin-Resource-Policy": "same-origin" }).end(JSON.stringify({ token: relayToken, phone }));
        return;
    }
    if (requested.startsWith("/relay/")) {
        handleRelay(request, response, requested.slice("/relay/".length));
        return;
    }
    const file = path.join(root, requested === "/" ? options.page : requested);

    // a request must not escape the served directory, however it is spelled
    if (!file.startsWith(root)) {
        response.writeHead(403).end("forbidden");
        return;
    }

    const headers = {
        "Cross-Origin-Opener-Policy": "same-origin",
        "Cross-Origin-Embedder-Policy": "require-corp",
        "Cross-Origin-Resource-Policy": "same-origin",
        "Cache-Control": "no-store, no-cache, must-revalidate, max-age=0",
        "Pragma": "no-cache",
        "Expires": "0",
    };

    if (!existsSync(file) || statSync(file).isDirectory()) {
        response.writeHead(404, headers).end("not found");
        process.stderr.write(`  404 ${requested}\n`);
        return;
    }
    headers["Content-Type"] = kContentTypes[path.extname(file).toLowerCase()] ?? "application/octet-stream";
    headers["Content-Length"] = statSync(file).size;
    response.writeHead(200, headers);
    createReadStream(file).pipe(response);
};

const server = options.https ? createHttpsServer(selfSignedCertificate(), handle) : createHttpServer(handle);
server.on("error", (error) => {
    process.stderr.write(`cannot listen on ${options.host}:${options.port}: ${error.message}\n`);
    process.exit(1);
});

server.listen(options.port, options.host, () => {
    const port = server.address().port;
    const scheme = options.https ? "https" : "http";
    // a test harness asks for port 0 and reads the chosen port from here rather than guessing a free one
    process.stdout.write(`listening on port ${port}\n`);

    const url = `${scheme}://localhost:${port}/${options.page}`;
    process.stdout.write(`serving ${root}\n  ${url}  (COOP/COEP set, so SharedArrayBuffer is available)\n`);
    if (options.host !== "127.0.0.1" && options.host !== "localhost") {
        process.stdout.write(`  ${scheme}://${localAddress()}:${port}/${options.page}  (from another machine on this network)\n`);
        if (!options.https) {
            process.stdout.write("  warning: that address is not a secure context, so SharedArrayBuffer is unavailable and the\n           threaded runtime will not start -- add --https\n");
        }
    }
    if (relayToken) {
        const where = options.host !== "127.0.0.1" && options.host !== "localhost" ? localAddress() : "localhost";
        process.stdout.write(`relay token: ${relayToken}\n  audience: ${scheme}://${where}:${port}/${options.page}?relay=${relayToken}\n  presenter (phone): ${scheme}://${where}:${port}/${options.page}?presenter&relay=${relayToken}  (or press Q on the audience page for a QR code)\n`);
    }
    process.stdout.write("  press Ctrl-C to stop\n");

    if (options.browser) {
        const opener = process.platform === "darwin" ? "open" : "xdg-open";
        try {
            execFileSync(opener, [url], { stdio: "ignore" });
        } catch {
            // no desktop session, or no opener: serving is what matters
        }
    }
});

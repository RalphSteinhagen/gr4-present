// The page's service worker, registered by shell.html, two jobs in one file: every response it serves carries the
// COOP/COEP headers the threaded runtime needs for SharedArrayBuffer, so a static host that sends none (GitHub Pages)
// works after one reload (the trick of coi-serviceworker, Guido Zuidhof, MIT); and an offline copy: the viewer's own
// files are kept per build and answered from the cache first, every other file the page reads (its deck, the default
// one included) is fetched and kept, and answered from the cache once the network fails. A talk seen online once
// presents offline. The build id below is stamped after each link: a hash of the page, the glue and the WASM. A deploy
// changes the id, so the browser installs the new worker, which fetches its build whole before it takes over.
const kBuild = "@GR4_PRESENT_BUILD_ID@";
const kViewerFiles = ["index.js", "index.wasm"]; // immutable within one build; the page itself is a navigation
// kept from the start, as no fetch of a normal visit passes them through the worker: the page under its own name (a
// visit is a navigation to the folder) and the worker itself; with them, "download viewer + deck" works offline
const kPageFiles = ["index.html", "gr4-serviceworker.js", "manifest.webmanifest", "icon.png"];

if (typeof window === "undefined") {
  // a viewer's caches are named after its scope: two viewers on one site (the full one at /, the minimal one at /minimal/)
  // keep their copies apart and drop only their own older builds
  const kCachePrefix = `gr4-present-${new URL(self.registration.scope).pathname}-`;
  const kCache = kCachePrefix + kBuild;
  // past the browser's HTTP cache, which a static host fills for minutes (GitHub Pages: 600 s), so a new build never
  // keeps the files of the one before; an unchanged file costs a 304
  const fresh = (url) => fetch(url, { cache: "no-cache" });
  const keep = async (cache, name) => {
    const url = new URL(name, self.location.href).href;
    const response = await fresh(url);
    if (!response.ok) throw new Error(`${name}: ${response.status}`);
    await cache.put(url, response);
  };
  // replacing a build, the viewer's files must all arrive, or the install fails and the build before stays until the
  // browser tries again; a first visit keeps them as its page reads them, rather than downloading them twice
  const keepBuild = async () => {
    const cache = await caches.open(kCache);
    await Promise.all(kPageFiles.map((name) => keep(cache, name).catch(() => {}))); // a missing one does not stop the worker
    if (self.registration.active) await Promise.all(kViewerFiles.map((name) => keep(cache, name)));
  };
  self.addEventListener("install", (event) => event.waitUntil(keepBuild()));
  // an updated worker waits until the page has started its runtime: its threads are made at start-up and none later
  // (PTHREAD_POOL_SIZE_STRICT), so from then on the page reads nothing of its build; earlier it would get the new
  // build's glue for the old one's page. No tab is reloaded: the next load or visit opens the new build.
  self.addEventListener("message", (event) => {
    if (event.data === "gr4-runtime-started") self.skipWaiting();
  });

  const isolated = (response) => {
    if (response.status === 0) return response; // opaque: nothing to rewrite, and the body cannot be read
    const headers = new Headers(response.headers);
    headers.set("Cross-Origin-Embedder-Policy", "require-corp");
    headers.set("Cross-Origin-Opener-Policy", "same-origin");
    headers.set("Cross-Origin-Resource-Policy", "cross-origin");
    return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
  };
  // an update's install has kept this build whole, so the build before is no longer needed, not even offline
  const dropOtherBuilds = async () => {
    for (const name of await caches.keys()) {
      if (name.startsWith(kCachePrefix) && name !== kCache) await caches.delete(name);
    }
  };
  self.addEventListener("activate", (event) => event.waitUntil(dropOtherBuilds().then(() => self.clients.claim())));
  // `keepWhile`: the worker stays up until the copy is kept
  const answer = async (request, keepWhile) => {
    const url = new URL(request.url);
    const navigation = request.mode === "navigate";
    const viewerFile = url.origin === self.location.origin && kViewerFiles.includes(url.pathname.split("/").pop());
    // a file by its extension; a poll without one (relay/<token>, relay-link) is never kept
    const keepable = navigation || viewerFile || /\.[A-Za-z0-9]+$/.test(url.pathname);
    const cache = await caches.open(kCache);
    const kept = (store) => store.match(request, { ignoreSearch: navigation }); // the page, whatever its query
    if (navigation || viewerFile) {
      const copy = await kept(cache);
      if (copy) return isolated(copy);
    }
    try {
      // a navigation's request cannot be fetched with options of its own, so its address is
      const response = await (navigation || viewerFile ? fresh(request.url) : fetch(request));
      // a copy the cache cannot take (its storage full or refused) costs the copy, never the answer
      if (keepable && response.status === 200 && (response.type === "basic" || response.type === "cors") && !response.redirected) {
        keepWhile(cache.put(request, response.clone()).catch(() => {}));
      }
      return isolated(response);
    } catch (error) {
      // a viewer file only from its own build's cache: the previous build's would not match this page
      const copy = (await kept(cache)) || (viewerFile ? undefined : await kept(caches));
      if (copy) return isolated(copy);
      throw error;
    }
  };
  self.addEventListener("fetch", (event) => {
    const request = event.request;
    if (request.method !== "GET" || (request.cache === "only-if-cached" && request.mode !== "same-origin")) return;
    event.respondWith(answer(request, (keeping) => event.waitUntil(keeping)));
  });
} else {
  (() => {
    // no worker without a secure context (http://<lan address>), in Firefox's private mode or for a page opened as a
    // file (launch-chromium): the page runs as served
    if (!window.isSecureContext || !navigator.serviceWorker || location.protocol === "file:") return;
    // a development server on this computer sends the headers itself and rebuilds between reloads: no copy of its files;
    // a page the worker serves is isolated by the worker, not by such a server
    const development = window.crossOriginIsolated && !navigator.serviceWorker.controller && ["localhost", "127.0.0.1", "[::1]"].includes(location.hostname);
    if (development) return;
    const registered = navigator.serviceWorker.register(document.currentScript.src);
    registered.catch((error) => {
      console.error("service worker not registered:", error);
      if (!window.crossOriginIsolated && window.gr4Present) window.gr4Present.setStatus("failed: the service worker was not registered: " + error);
    });
    // a new build's worker, installed now or later, takes over once this page's runtime runs (the worker says why)
    window.addEventListener("gr4-runtime-started", () =>
      registered.then(
        (registration) => {
          const adopt = (worker) => {
            if (worker?.state === "installed") worker.postMessage("gr4-runtime-started");
            else worker?.addEventListener("statechange", () => worker.state === "installed" && worker.postMessage("gr4-runtime-started"));
          };
          adopt(registration.waiting ?? registration.installing);
          registration.addEventListener("updatefound", () => adopt(registration.installing));
        },
        () => {}, // reported above
      ),
    );
    if (window.crossOriginIsolated) return; // the host sends the headers; the worker only keeps the offline copy
    // once the worker controls this page it re-serves it with the headers: reload then, once per tab, so a host that
    // strips them cannot loop
    const kReloaded = "gr4ReloadedForIsolation";
    if (sessionStorage.getItem(kReloaded)) {
      sessionStorage.removeItem(kReloaded);
      return;
    }
    if (navigator.serviceWorker.controller) return; // already served by the worker: a new build taking over reloads nothing
    navigator.serviceWorker.addEventListener("controllerchange", () => {
      sessionStorage.setItem(kReloaded, "1");
      location.reload();
    });
  })();
}

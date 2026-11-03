// The page's service worker, registered by shell.html, two jobs in one file: every response it serves carries the
// COOP/COEP headers the threaded runtime needs for SharedArrayBuffer, so a static host that sends none (GitHub Pages)
// works after one reload (the trick of coi-serviceworker, Guido Zuidhof, MIT); and an offline copy: the viewer's own
// files are kept per build and answered from the cache first, every other file the page reads (its deck, the default
// one included) is fetched and kept, and answered from the cache once the network fails. A talk seen online once
// presents offline. The build id below is stamped after each link: a hash of the page, the glue and the WASM.
const kBuild = "@GR4_PRESENT_BUILD_ID@";
const kCachePrefix = "gr4-present-";
const kCache = kCachePrefix + kBuild;
const kViewerFiles = ["index.js", "index.wasm"]; // immutable within one build; the page itself is a navigation

if (typeof window === "undefined") {
  // no skipWaiting: an updated worker takes over once the old one's tabs are closed, never halfway through a page's
  // load, where it would hand out a runtime from the new build to a page from the old one
  self.addEventListener("activate", (event) => event.waitUntil(self.clients.claim()));

  const isolated = (response) => {
    if (response.status === 0) return response; // opaque: nothing to rewrite, and the body cannot be read
    const headers = new Headers(response.headers);
    headers.set("Cross-Origin-Embedder-Policy", "require-corp");
    headers.set("Cross-Origin-Opener-Policy", "same-origin");
    headers.set("Cross-Origin-Resource-Policy", "cross-origin");
    return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
  };
  // the build before this one is kept until this one holds its runtime, so a deploy met offline still presents
  const dropOtherBuilds = async () => {
    for (const name of await caches.keys()) {
      if (name.startsWith(kCachePrefix) && name !== kCache) await caches.delete(name);
    }
  };
  const answer = async (request) => {
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
      const response = await fetch(request);
      if (keepable && response.status === 200 && (response.type === "basic" || response.type === "cors") && !response.redirected) {
        await cache.put(request, response.clone());
        if (url.pathname.endsWith("index.wasm")) await dropOtherBuilds();
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
    event.respondWith(answer(request));
  });
} else {
  (() => {
    // no worker without a secure context (http://<lan address>) or in Firefox's private mode: the page runs as served
    if (!window.isSecureContext || !navigator.serviceWorker) return;
    // a development server on this computer sends the headers itself and rebuilds between reloads: no copy of its files
    const development = window.crossOriginIsolated && ["localhost", "127.0.0.1", "[::1]"].includes(location.hostname);
    if (development) return;
    navigator.serviceWorker.register(document.currentScript.src).catch((error) => {
      console.error("service worker not registered:", error);
      if (!window.crossOriginIsolated && window.gr4Present) window.gr4Present.setStatus("failed: the service worker was not registered: " + error);
    });
    if (window.crossOriginIsolated) return; // the host sends the headers; the worker only keeps the offline copy
    // once the worker controls this page it re-serves it with the headers: reload then, once per tab, so a host that
    // strips them cannot loop
    const kReloaded = "gr4ReloadedForIsolation";
    if (sessionStorage.getItem(kReloaded)) {
      sessionStorage.removeItem(kReloaded);
      return;
    }
    navigator.serviceWorker.addEventListener("controllerchange", () => {
      sessionStorage.setItem(kReloaded, "1");
      location.reload();
    });
  })();
}

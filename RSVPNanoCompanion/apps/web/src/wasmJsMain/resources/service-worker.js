const CACHE = "rsvpnano-web-v2";
const SHELL = ["./", "./index.html", "./manifest.webmanifest", "./icon.svg"];

self.addEventListener("install", event => {
  event.waitUntil(
    fetch("./asset-manifest.json", { cache: "no-store" })
      .then(response => response.ok ? response.json() : SHELL)
      .catch(() => SHELL)
      .then(assets => caches.open(CACHE).then(cache => cache.addAll(assets)))
  );
  self.skipWaiting();
});

self.addEventListener("activate", event => {
  event.waitUntil(caches.keys().then(keys => Promise.all(keys.filter(key => key !== CACHE).map(key => caches.delete(key)))));
  self.clients.claim();
});

self.addEventListener("fetch", event => {
  if (event.request.method !== "GET") return;
  const url = new URL(event.request.url);
  if (url.origin !== self.location.origin) return;

  const networkFirst = event.request.mode === "navigate" ||
    url.pathname.includes("/firmware/") ||
    /\.(?:html|js|wasm)$/.test(url.pathname);
  event.respondWith(networkFirst
    ? fetch(event.request).then(response => {
        if (response.ok) caches.open(CACHE).then(cache => cache.put(event.request, response.clone()));
        return response;
      }).catch(() => caches.match(event.request))
    : caches.match(event.request).then(cached => cached || fetch(event.request).then(response => {
        if (response.ok) caches.open(CACHE).then(cache => cache.put(event.request, response.clone()));
        return response;
      }))
  );
});

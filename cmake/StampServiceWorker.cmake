# Writes the service worker with the build id its cache is named after: the hash of the page, the JavaScript glue and
# the WASM it serves, so every build gets a cache of its own and a rebuild never answers from the one before. Run after
# the link: cmake -DSOURCE=<gr4-serviceworker.js> -DPAGE=<index.html> -DGLUE=<index.js> -DWASM=<index.wasm>
# -DTARGET=<viewer-web/gr4-serviceworker.js> -P StampServiceWorker.cmake
file(SHA256 ${PAGE} _page_hash)
file(SHA256 ${GLUE} _glue_hash)
file(SHA256 ${WASM} _wasm_hash)
string(SHA256 _build_hash "${_page_hash}${_glue_hash}${_wasm_hash}")
string(
  SUBSTRING ${_build_hash}
            0
            12
            GR4_PRESENT_BUILD_ID)
configure_file(${SOURCE} ${TARGET} @ONLY)

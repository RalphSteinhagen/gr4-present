#ifndef GR4_PRESENT_PRESENTER_BUNDLE_HPP
#define GR4_PRESENT_PRESENTER_BUNDLE_HPP

#include <gr4-present/PresentationLoader.hpp>

#include <array>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gr::present {

/// "download viewer + deck": the site's files as served, the deck shown under `default/` (what the page opens without
/// an address), the start scripts per platform (a small server, or Chromium with two switches) and a README, as `<folder>/` in one `.tar.gz`. Unpacked and started by its
/// script, the viewer presents without a network on a computer that never saw the site; `index.html` names the site in
/// its `gr4-site` meta, the no-install alternative its launch screen offers when the page is opened as a file
struct PresenterBundle {
    std::string_view             site;      // the page's folder address, https://host/gr4-present
    std::string_view             title;     // the deck's title, for the README; empty for a deck without one
    std::span<const PackageFile> siteFiles; // `kSiteFiles`, by their names
    std::span<const PackageFile> deckFiles; // the deck shown by its package-relative paths; none while no deck is loaded
};

/// what the viewer fetches from its own folder for the bundle
inline constexpr std::array<std::string_view, 6UZ> kSiteFiles{"index.html", "index.js", "index.wasm", "gr4-serviceworker.js", "manifest.webmanifest", "icon.png"};

[[nodiscard]] std::expected<std::vector<std::byte>, std::string> packPresenterBundle(std::string_view folder, const PresenterBundle& bundle);

} // namespace gr::present

#endif // GR4_PRESENT_PRESENTER_BUNDLE_HPP

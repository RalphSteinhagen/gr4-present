#ifndef GR4_PRESENT_DIAGNOSTICS_PANEL_HPP
#define GR4_PRESENT_DIAGNOSTICS_PANEL_HPP

#include "Theme.hpp"

#include <gr4-present/Diagnostics.hpp>

#include <string>

namespace gr::present {

/// this viewer's version, commit and build time, as the diagnostics panel shows them
[[nodiscard]] std::string buildStamp();

/// what went wrong, as a list a presenter can read at a glance; opened from the side menu, never by itself
struct DiagnosticsPanel {
    bool        open  = false;
    std::string build = buildStamp(); // so a viewer kept in a browser's cache can be told from a new one

    void draw(const Diagnostics& diagnostics, const Theme& theme, float interfacePixelsPerPoint) const;
};

} // namespace gr::present

#endif // GR4_PRESENT_DIAGNOSTICS_PANEL_HPP

#ifndef GR4_PRESENT_DIAGNOSTICS_PANEL_HPP
#define GR4_PRESENT_DIAGNOSTICS_PANEL_HPP

#include "Theme.hpp"

#include <gr4-present/Diagnostics.hpp>

namespace gr::present {

/// what went wrong, as a list a presenter can read at a glance; opened from the side menu, never by itself
struct DiagnosticsPanel {
    bool open = false;

    void draw(const Diagnostics& diagnostics, const Theme& theme, float interfacePixelsPerPoint) const;
};

} // namespace gr::present

#endif // GR4_PRESENT_DIAGNOSTICS_PANEL_HPP

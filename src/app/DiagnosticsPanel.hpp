#ifndef GR4_PRESENT_DIAGNOSTICS_PANEL_HPP
#define GR4_PRESENT_DIAGNOSTICS_PANEL_HPP

#include "Theme.hpp"

#include <gr4-present/Diagnostics.hpp>

namespace gr::present {

/**
 * What went wrong, in a list a presenter can read before standing up.
 *
 * The same records go to GR4's logger, which is where a trace belongs; this is the glance. It is opened from the
 * side menu rather than appearing by itself, because a problem with one figure should not interrupt a talk.
 */
struct DiagnosticsPanel {
    bool open = false;

    void draw(const Diagnostics& diagnostics, const Theme& theme) const;
};

} // namespace gr::present

#endif // GR4_PRESENT_DIAGNOSTICS_PANEL_HPP

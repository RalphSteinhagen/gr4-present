#ifndef GR4_PRESENT_PLOT_VIEW_HPP
#define GR4_PRESENT_PLOT_VIEW_HPP

#include "Canvas.hpp"
#include "Theme.hpp"

#include <gr4-present/Plot.hpp>
#include <gr4-present/RegionGeometry.hpp>

namespace gr::present {

/// a plot's height as a fraction of the width it is given
inline constexpr float kPlotAspect = 0.52f;

/// draws a plot with `ImDrawList` only: ImPlot arrives only with OpenDigitizer and needs a local patch to build here
/// ticks follow Heckbert's nice-number rule: P. S. Heckbert, "Nice numbers for graph labels", in Graphics Gems,
/// A. S. Glassner, Ed. Boston, MA, USA: Academic Press, 1990, pp. 61-63
void drawPlot(Canvas canvas, const Theme& theme, const Plot& plot, const Rectangle& box, float size);

} // namespace gr::present

#endif // GR4_PRESENT_PLOT_VIEW_HPP

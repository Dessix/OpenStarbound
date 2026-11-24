#pragma once

#include "StarRect.hpp"
#include "StarList.hpp"
#include "StarMaybe.hpp"

namespace Star {

// Where a tooltip and the sections scripts attach beside it go on screen. All
// values are in interface pixels with y up; nothing here touches panes, so the
// rules can be tested on their own.

enum class TooltipSide { Left, Right, Below, Above };

struct TooltipSection {
  // Sides to try, in order of preference. Must not be empty.
  List<TooltipSide> sides;
  // Size of the section's art, drop shadow and margins included.
  Vec2I size;
  // Transparent art (shadow, margins) around the visible panel.
  RectI margins = RectI(0, 0, 0, 0);
};

// Placement options: the stock panel's art margins (left, bottom, right, top
// in a RectI's min/max) and the space between visible panel edges.
struct TooltipSpacing {
  RectI stockMargins = RectI(0, 0, 0, 0);
  int gap = 0;
};

struct TooltipPlacement {
  // Bottom left of the stock panel.
  Vec2I stockPosition;
  // Side and screen rect of each section, in the order given. A rect shorter
  // than the section clips its content, which is scrolled.
  List<TooltipSide> sides;
  List<RectI> rects;
};

// The stock panel's position P0 is the stock game's position for a tooltip of
// its size: beside the cursor, flipped left or down when it would overflow,
// then kept on screen. A tooltip without sections is placed there, always.
//
// Layout for a given stock position and one side per section (layoutTooltip):
// * Left/right sections first, in order, each further out than the sections
//   before it on the same side, `gap` apart measured between visible edges
//   (art minus margins); top level with the stock
//   panel's top, slid along the shared edge just far enough to stay on screen;
//   clipped to the window height.
// * Then below/above sections, in order, clipped to the window height minus
//   the stock panel's and the earlier below/above sections' heights. One
//   shares the stock panel's left edge and extends right (also under a short
//   left/right section); if that leaves the screen or comes within `gap` of a
//   left/right section, it shares the right edge and extends left; failing
//   both, it slides onto the screen from the left-edge, then the right-edge
//   position. Vertically it sits directly under (over) the stock panel, or
//   under (over) the earlier below/above sections it overlaps horizontally.
//   If it still comes within `gap` of a left/right section, the layout does
//   not fit.
// No rect overlaps another; a layout fits when every rect is on screen.
//
// Search (placeTooltip), first match wins. A section is a left/right or a
// below/above section by its first listed side.
// 0. Left/right sections take precedence: they are placed by this search
//    alone, and if the below/above sections fit around that placement (some
//    combination of their sides), it is used.
// 1. The stock panel at P0, side combinations in order: the latched sides
//    first, then by each section's preference, left/right sections
//    varying slowest, then earlier sections before later ones.
// 2. The stock panel moved: the combination and position with the smallest
//    move (|dx| + |dy|), ties in the order above.
// 3. None fits anywhere: the stock panel at P0, the first combination, and
//    each section pushed onto the screen, overlapping if need be.
//
// `window` is the window size in interface pixels (it may be fractional at
// fractional UI scales). Sizes include the art's shadow.
TooltipPlacement placeTooltip(Vec2F const& window, Vec2I const& mouse, Vec2I const& mouseOffset,
    Vec2I const& stockSize, List<TooltipSection> const& sections,
    List<TooltipSide> const& latched = {}, TooltipSpacing const& spacing = {});

struct TooltipLayout {
  List<RectI> rects;
  bool fits = false;
  // the rects before sliding onto the screen (left/right panels: vertical
  // position only; below/above: horizontal), as candidate moves for the search
  List<RectI> unslid;
};

// The layout rule above, for a stock panel at `stock` and sections on `sides`.
// `screen` is the window size in interface pixels, as for placeTooltip.
// `force` pushes rects that do not fit onto the screen.
TooltipLayout layoutTooltip(Vec2F const& screen, Vec2I const& stock, Vec2I const& stockSize,
    List<TooltipSection> const& sections, List<TooltipSide> const& sides, bool force = false,
    TooltipSpacing const& spacing = {});

// How far (in interface pixels, down from the top) an over-tall section is
// scrolled `elapsed` seconds after it was displayed: it holds at the top for
// `hold` seconds, scrolls down at `speed` pixels per second, holds at the
// bottom for `hold` seconds, returns to the top and repeats. Returns 0 when
// `contentHeight` fits in `visibleHeight` or `speed` is not positive.
int tooltipScrollOffset(float elapsed, int contentHeight, int visibleHeight, float hold, float speed);

}

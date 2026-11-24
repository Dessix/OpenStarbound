#pragma once

#include "StarPane.hpp"
#include "StarTooltipPlacement.hpp"

namespace Star {

// Grows a pane's background beyond its stock art: strips inserted into the
// stock panel above its footer, and separate panels (sections) beside, below
// or above it, which PaneManager places on screen for tooltips. Also carries
// what a tooltip pane shows. Owned by its pane (Pane::extensions()), which
// exists only once something has used it, so other panes pay nothing.
class PaneExtensions {
public:
  struct Extension {
    // The side of a section; none for a strip.
    Maybe<TooltipSide> side;
    // Width of a section, or height of a strip.
    int size = 0;
    // Images drawn for the new region; empty entries fall back to the pane's
    // own header / body / footer. Strips only use body.
    Pane::BGResult images;
    // The 1px row (from the bottom) of the body image stretched over the region.
    Maybe<int> bodyRow;
    // Sections only: the sides PaneManager may move the section to, in order
    // of preference (empty means just `side`), and its height (default: the
    // height of the stock panel).
    List<TooltipSide> sides;
    Maybe<int> height;

    // Set by the pane: the widget laid out over a section; the section's art
    // rect relative to the stock panel's bottom left as placed by PaneManager
    // (none: level with its top, outside the stock art); how far its art
    // reaches left and right of the visible panel (its shadow and margins);
    // the visible panel's rect and the art's rect in pane coordinates; the
    // content's full height (taller than rect when the section is clipped)
    // and how far it is scrolled down.
    WidgetPtr content;
    Maybe<RectI> placed;
    Vec2I artMargins;
    RectI rect;
    RectI artRect;
    int contentHeight = 0;
    int scroll = 0;
  };
  // Measurements of a pane background image (see panelArt).
  struct PanelArt {
    int width = 0;
    int height = 0;
    // opaque columns [left, right)
    int left = 0;
    int right = 0;
    // the column stretched to widen the art
    int split = 0;
    // rows at the bottom below the opaque panel (the shadow), and at the top
    // above it
    int bottom = 0;
    int top = 0;
    // the row with the fewest pixels unlike its split column inside the
    // opaque columns, nearest the middle: a row without inner decoration
    int plainRow = 0;
  };

  explicit PaneExtensions(Pane* pane);

  // Describes what a tooltip pane shows ("item", "recipe", "text"); set by the
  // tooltip builders.
  void setTooltipInfo(String kind, ItemPtr item = {});
  String const& tooltipKind() const;
  ItemPtr const& tooltipItem() const;

  // Grows the pane's background by a strip at the bottom of the stock panel
  // (inserted above the footer, moving everything above the footer up) or by a
  // section beside it, and returns the new region in pane coordinates. For a
  // section, `content` is kept over the section as it moves; sections move
  // the stock content as needed to stay in the pane's bounds.
  RectI extendBackground(Extension const& extension, WidgetPtr const& content = {});
  // Total height of the strips, and total width of the sections on a side.
  int stripSize() const;
  int sectionSize(TooltipSide side) const;
  bool hasSections() const;

  List<Extension> const& extensions() const;
  // Size of the stock panel including strips, its offset in the pane, and
  // the x (from its left) where right-hand sections join it.
  Vec2I stockSize() const;
  Vec2I stockOffset() const;
  int stockJoin() const;
  // Transparent art around the visible stock panel and around a section's
  // visible panel: left, bottom (min) and right, top (max).
  RectI stockMargins() const;
  RectI extensionMargins(Extension const& extension) const;
  // Moves section i (counting sections only) to placements[i]: a side and its
  // art rect relative to the stock panel's bottom left. A rect lower than the
  // section's height clips it.
  void placeExtensions(List<pair<TooltipSide, RectI>> const& placements);
  // Scrolls the content of section i down by offsets[i] (clamped to what is
  // hidden). Drawing only; layout is unaffected.
  void setExtensionScroll(List<int> const& offsets);

  // Called by Pane: draws the extensions behind (beforeStock) or in front of
  // the stock background, and draws the pane's children if a section is
  // clipped (returning false otherwise, when the default drawing applies).
  void render(bool beforeStock);
  bool drawChildren();

private:
  PanelArt const& panelArt(String const& image) const;
  Pane::BGResult extensionImages(Extension const& extension) const;
  void layoutExtensions();

  Pane* m_pane;
  String m_tooltipKind;
  ItemPtr m_tooltipItem;
  List<Extension> m_extensions;
  Vec2I m_baseSize;
  Vec2I m_stockOffset;
  mutable StringMap<PanelArt> m_panelArt;
};

}

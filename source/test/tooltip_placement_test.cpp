#include "StarTooltipPlacement.hpp"

#include "gtest/gtest.h"

#include <random>

using namespace Star;

namespace {

Vec2I const Offset(10, -10);  // /panes.config:tooltipMouseoverOffset is of this shape
Vec2I const Stock(151, 120);
TooltipSection const Crafting{{TooltipSide::Right, TooltipSide::Left}, Vec2I(195, 300)};

Vec2F windowAt(float scale) {
  return Vec2F(1920, 1080) / scale;
}

RectI stockRect(TooltipPlacement const& p) {
  return RectI::withSize(p.stockPosition, Stock);
}

// compared against the fractional window size, unrounded
bool onScreen(RectI const& rect, Vec2F const& window) {
  return rect.xMin() >= 0 && rect.yMin() >= 0 && rect.xMax() <= window[0] && rect.yMax() <= window[1];
}

// the stock rule, written out separately from the implementation
Vec2I stockAlone(Vec2F const& window, Vec2I const& mouse) {
  Vec2I p;
  p[0] = mouse[0] + Offset[0] + Stock[0] > window[0] ? mouse[0] - Offset[0] - Stock[0] : mouse[0] + Offset[0];
  p[1] = mouse[1] + Offset[1] - Stock[1] < 0 ? mouse[1] - Offset[1] : mouse[1] + Offset[1] - Stock[1];
  return p;
}

}

TEST(TooltipPlacementTest, NoSectionsMatchesStock) {
  for (float scale : {1.0f, 1.5f, 2.0f}) {
    Vec2F window = windowAt(scale);
    for (Vec2I mouse : {Vec2I(5, 5), Vec2I(window[0] - 5, 5), Vec2I(5, window[1] - 5), Vec2I(window[0] - 5, window[1] - 5), Vec2I(window[0] / 2, window[1] / 2)}) {
      auto p = placeTooltip(window, mouse, Offset, Stock, {});
      EXPECT_EQ(p.stockPosition, stockAlone(window, mouse)) << scale << " " << mouse;
    }
  }
}

TEST(TooltipPlacementTest, CornersAndEdges) {
  for (float scale : {1.0f, 1.5f, 2.0f}) {
    Vec2F window = windowAt(scale);
    int w = window[0], h = window[1];
    struct Case { Vec2I mouse; TooltipSide side; };
    List<Case> cases = {
      {{5, h - 5}, TooltipSide::Right},          // top left
      {{w - 5, h - 5}, TooltipSide::Left},       // top right
      {{5, 5}, TooltipSide::Right},              // bottom left
      {{w - 5, 5}, TooltipSide::Left},           // bottom right
      {{w / 2, h - 5}, TooltipSide::Right},      // top edge
      {{w / 2, 5}, TooltipSide::Right},          // bottom edge
      {{5, h / 2}, TooltipSide::Right},          // left edge
      {{w - 5, h / 2}, TooltipSide::Left},       // right edge
      {{w - 200, h / 2}, TooltipSide::Left},     // stock fits on the right, the section does not
    };
    for (auto const& c : cases) {
      auto p = placeTooltip(window, c.mouse, Offset, Stock, {Crafting});
      ASSERT_EQ(p.sides.size(), 1u);
      EXPECT_EQ(p.sides[0], c.side) << scale << " " << c.mouse;
      // the stock panel is where the stock game puts it
      EXPECT_EQ(p.stockPosition, stockAlone(window, c.mouse)) << scale << " " << c.mouse;
      EXPECT_TRUE(onScreen(p.rects[0], window)) << scale << " " << c.mouse << " " << p.rects[0];
      // art edge to art edge with the stock panel
      if (c.side == TooltipSide::Right)
        EXPECT_EQ(p.rects[0].xMin(), p.stockPosition[0] + Stock[0]);
      else
        EXPECT_EQ(p.rects[0].xMax(), p.stockPosition[0]);
    }
  }
}

TEST(TooltipPlacementTest, DefaultAlignmentIsTopLevel) {
  Vec2F window = windowAt(1);
  auto p = placeTooltip(window, Vec2I(400, 800), Offset, Stock, {Crafting});
  EXPECT_EQ(p.rects[0].yMax(), stockRect(p).yMax());
  // equal heights line up exactly, as before sections could move
  TooltipSection same{{TooltipSide::Right}, Vec2I(195, Stock[1])};
  p = placeTooltip(window, Vec2I(400, 800), Offset, Stock, {same});
  EXPECT_EQ(p.rects[0].yMin(), stockRect(p).yMin());
}

TEST(TooltipPlacementTest, TallSectionShortStockBottomRight) {
  for (float scale : {1.0f, 1.5f, 2.0f}) {
    Vec2F window = windowAt(scale);
    Vec2I mouse(window[0] - 3, 3);
    auto p = placeTooltip(window, mouse, Offset, Stock, {Crafting});
    // item panel stays at the cursor, crafting goes left of it and rises to sit on the screen bottom
    EXPECT_EQ(p.stockPosition, stockAlone(window, mouse));
    EXPECT_EQ(p.sides[0], TooltipSide::Left);
    EXPECT_EQ(p.rects[0].xMax(), p.stockPosition[0]);
    EXPECT_EQ(p.rects[0].yMin(), 0);
    EXPECT_GT(p.rects[0].yMax(), stockRect(p).yMax());
  }
}

TEST(TooltipPlacementTest, SlidesOnlyAsFarAsNeeded) {
  Vec2F window = windowAt(1);
  // stock top at 300 - 10 = 290; a 300 tall section level with it would end at -10
  auto p = placeTooltip(window, Vec2I(400, 300), Offset, Stock, {Crafting});
  EXPECT_EQ(p.rects[0].yMin(), 0);
  EXPECT_EQ(p.rects[0].yMax(), 300);
}

TEST(TooltipPlacementTest, StacksOutwards) {
  Vec2F window = windowAt(1);
  TooltipSection narrow{{TooltipSide::Right, TooltipSide::Left}, Vec2I(50, 100)};
  auto p = placeTooltip(window, Vec2I(400, 800), Offset, Stock, {narrow, narrow});
  EXPECT_EQ(p.rects[1].xMin(), p.rects[0].xMax());
}

// Before below/above panels this moved only the section, over the stock panel.
// Panels must not overlap (V15), so now the stock panel moves just far enough.
TEST(TooltipPlacementTest, NoSideFitsMovesStockByTheMinimum) {
  // 500 wide screen: the section fits on neither side of the stock panel at its stock position
  Vec2F window(500, 400);
  TooltipSection wide{{TooltipSide::Right, TooltipSide::Left}, Vec2I(300, 100)};
  Vec2I mouse(60, 300);
  auto p = placeTooltip(window, mouse, Offset, Stock, {wide});
  EXPECT_EQ(p.sides[0], TooltipSide::Right);
  EXPECT_EQ(p.stockPosition, stockAlone(window, mouse) - Vec2I(21, 0));
  EXPECT_EQ(p.rects[0].xMin(), p.stockPosition[0] + Stock[0]);
  EXPECT_EQ(p.rects[0].xMax(), 500);
  EXPECT_TRUE(onScreen(p.rects[0], window));
}

TEST(TooltipPlacementTest, NothingFitsPushesOntoScreen) {
  // wider than the screen together: the old last resort
  Vec2F window(400, 400);
  TooltipSection wide{{TooltipSide::Right, TooltipSide::Left}, Vec2I(300, 100)};
  Vec2I mouse(60, 300);
  auto p = placeTooltip(window, mouse, Offset, Stock, {wide});
  EXPECT_EQ(p.sides[0], TooltipSide::Right);
  EXPECT_EQ(p.stockPosition, stockAlone(window, mouse));
  EXPECT_EQ(p.rects[0].xMax(), 400);
}

// 1366x768 at scale 1.5 is 910.67x512 interface pixels; a rect reaching 911
// would sit half a device pixel off screen
TEST(TooltipPlacementTest, FractionalWindowSize) {
  Vec2F window = Vec2F(1366, 768) / 1.5f;
  int h = window[1];
  TooltipSection tall{{TooltipSide::Right, TooltipSide::Left}, Vec2I(195, 3 * h)};
  for (Vec2I mouse : {Vec2I(5, 5), Vec2I(450, 250), Vec2I(905, 5), Vec2I(905, 505)}) {
    for (auto const& section : {Crafting, tall, TooltipSection{{TooltipSide::Below, TooltipSide::Above}, Vec2I(905, 80)}}) {
      auto p = placeTooltip(window, mouse, Offset, Stock, {section});
      EXPECT_TRUE(onScreen(stockRect(p), window)) << mouse << " " << stockRect(p);
      EXPECT_TRUE(onScreen(p.rects[0], window)) << mouse << " " << p.rects[0];
    }
  }
}

TEST(TooltipPlacementTest, OverTallSectionLeavesStockAlone) {
  for (float scale : {1.0f, 1.5f, 2.0f}) {
    Vec2F window = windowAt(scale);
    int w = window[0], h = window[1];
    TooltipSection tall{{TooltipSide::Right, TooltipSide::Left}, Vec2I(195, 3 * h)};
    for (Vec2I mouse : {Vec2I(5, 5), Vec2I(w - 5, 5), Vec2I(5, h - 5), Vec2I(w - 5, h - 5)}) {
      auto p = placeTooltip(window, mouse, Offset, Stock, {tall});
      EXPECT_EQ(p.stockPosition, stockAlone(window, mouse)) << scale << " " << mouse;
      EXPECT_TRUE(onScreen(stockRect(p), window)) << scale << " " << mouse;
      // clipped to the window height, fully on screen, beside the stock panel
      EXPECT_EQ(p.rects[0].height(), (int)std::floor(window[1])) << scale << " " << mouse;
      EXPECT_TRUE(onScreen(p.rects[0], window)) << scale << " " << mouse << " " << p.rects[0];
      EXPECT_FALSE(p.rects[0].intersects(stockRect(p), false)) << scale << " " << mouse;
    }
  }
}

TEST(TooltipScrollTest, Cycle) {
  float const hold = 1.5f, speed = 25;
  // content fits: never scrolls
  EXPECT_EQ(tooltipScrollOffset(0, 100, 200, hold, speed), 0);
  EXPECT_EQ(tooltipScrollOffset(5, 200, 200, hold, speed), 0);
  // 100 px hidden: 4 s of travel, 7 s per cycle
  EXPECT_EQ(tooltipScrollOffset(0, 300, 200, hold, speed), 0);
  EXPECT_EQ(tooltipScrollOffset(1.4f, 300, 200, hold, speed), 0);
  EXPECT_EQ(tooltipScrollOffset(2.5f, 300, 200, hold, speed), 25);
  EXPECT_EQ(tooltipScrollOffset(3.5f, 300, 200, hold, speed), 50);
  EXPECT_EQ(tooltipScrollOffset(5.6f, 300, 200, hold, speed), 100);
  EXPECT_EQ(tooltipScrollOffset(6.9f, 300, 200, hold, speed), 100);
  // back to the top, and the cycle repeats
  EXPECT_EQ(tooltipScrollOffset(7.1f, 300, 200, hold, speed), 0);
  EXPECT_EQ(tooltipScrollOffset(9.5f, 300, 200, hold, speed), 25);
  // never past the end, never negative, monotonic within a pass
  int last = 0;
  for (float t = 0; t < 7; t += 0.01f) {
    int offset = tooltipScrollOffset(t, 300, 200, hold, speed);
    EXPECT_GE(offset, last);
    EXPECT_LE(offset, 100);
    last = offset;
  }
  // a non-positive speed turns scrolling off
  EXPECT_EQ(tooltipScrollOffset(3, 300, 200, hold, 0), 0);
}

TEST(TooltipPlacementTest, LatchedSideIsKeptWhileItFits) {
  Vec2F window = windowAt(1);
  // left is latched although right would be preferred: keep left
  auto p = placeTooltip(window, Vec2I(900, 800), Offset, Stock, {Crafting}, {TooltipSide::Left});
  EXPECT_EQ(p.sides[0], TooltipSide::Left);
  // the latched side no longer fits: choose again by preference
  p = placeTooltip(window, Vec2I(100, 800), Offset, Stock, {Crafting}, {TooltipSide::Left});
  EXPECT_EQ(p.sides[0], TooltipSide::Right);
  // a latched side the section does not allow is ignored
  TooltipSection rightOnly{{TooltipSide::Right}, Vec2I(195, 300)};
  p = placeTooltip(window, Vec2I(900, 800), Offset, Stock, {rightOnly}, {TooltipSide::Left});
  EXPECT_EQ(p.sides[0], TooltipSide::Right);
}

// ---- below / above panels ---------------------------------------------------

namespace {

TooltipSection const Processing{{TooltipSide::Below, TooltipSide::Above}, Vec2I(195, 80)};

List<List<TooltipSide>> allCombos(List<TooltipSection> const& sections) {
  List<List<TooltipSide>> combos = {{}};
  for (auto const& s : sections) {
    List<List<TooltipSide>> next;
    for (auto const& c : combos)
      for (auto side : s.sides)
        next.append(c), next.last().append(side);
    combos = next;
  }
  return combos;
}

bool fitsAt(Vec2F const& window, Vec2I const& stock, Vec2I const& stockSize, List<TooltipSection> const& sections) {
  for (auto const& c : allCombos(sections))
    if (layoutTooltip(window, stock, stockSize, sections, c).fits)
      return true;
  return false;
}

// assertions every placement must satisfy when a fitting one exists
void checkPlacement(TooltipPlacement const& p, Vec2F const& window, Vec2I const& stockSize,
    List<TooltipSection> const& sections, std::string const& what) {
  SCOPED_TRACE(what);
  RectI stock = RectI::withSize(p.stockPosition, stockSize);
  ASSERT_EQ(p.rects.size(), sections.size());
  EXPECT_TRUE(onScreen(stock, window)) << stock;
  List<RectI> all = {stock};
  for (size_t i = 0; i < sections.size(); ++i) {
    EXPECT_TRUE(sections[i].sides.contains(p.sides[i]));
    EXPECT_TRUE(onScreen(p.rects[i], window)) << i << " " << p.rects[i];
    EXPECT_EQ(p.rects[i].width(), sections[i].size[0]);
    EXPECT_LE(p.rects[i].height(), sections[i].size[1]);
    all.append(p.rects[i]);
  }
  for (size_t i = 0; i < all.size(); ++i)
    for (size_t j = i + 1; j < all.size(); ++j)
      EXPECT_FALSE(all[i].intersects(all[j], false)) << i << " " << j << " " << all[i] << " " << all[j];

  // edge sharing (V14): a below/above panel shares the stock panel's left edge
  // and extends right; it shares the right edge only when the left-edge
  // position overlaps a side panel or leaves the screen; otherwise it slid
  int screenW = std::floor(window[0]);
  for (size_t i = 0; i < sections.size(); ++i) {
    auto const& r = p.rects[i];
    if (p.sides[i] != TooltipSide::Below && p.sides[i] != TooltipSide::Above)
      continue;
    bool slid = r.xMin() == 0 || r.xMax() == screenW;
    if (slid)
      continue;
    EXPECT_TRUE(r.xMin() == stock.xMin() || r.xMax() == stock.xMax()) << r;
    if (r.xMin() != stock.xMin()) {
      RectI leftEdge = r.translated(Vec2I(stock.xMin() - r.xMin(), 0));
      bool blocked = leftEdge.xMax() > screenW;
      for (size_t j = 0; j < sections.size(); ++j)
        if (p.sides[j] == TooltipSide::Left || p.sides[j] == TooltipSide::Right)
          blocked = blocked || leftEdge.intersects(p.rects[j], false);
      EXPECT_TRUE(blocked) << "extends left without need: " << r;
    }
  }
}

// side panels take precedence (V14): when the below/above panels have any
// placement around the side panels as they are placed alone, the side panels
// and the stock panel land exactly where they would without them
void checkSidePrecedence(TooltipPlacement const& p, Vec2F const& window, Vec2I const& mouse,
    Vec2I const& offset, Vec2I const& stockSize, List<TooltipSection> const& sections, std::string const& what) {
  SCOPED_TRACE(what);
  List<size_t> sideIdx;
  List<TooltipSection> sides;
  for (size_t i = 0; i < sections.size(); ++i) {
    auto first = sections[i].sides.first();
    if (first == TooltipSide::Left || first == TooltipSide::Right)
      sideIdx.append(i), sides.append(sections[i]);
  }
  if (sides.empty() || sides.size() == sections.size())
    return;
  auto alone = placeTooltip(window, mouse, offset, stockSize, sides);
  if (!layoutTooltip(window, alone.stockPosition, stockSize, sides, alone.sides).fits)
    return;
  bool any = false;
  for (auto const& c : allCombos(sections)) {
    bool same = true;
    for (size_t k = 0; k < sideIdx.size(); ++k)
      same = same && c[sideIdx[k]] == alone.sides[k];
    if (same && layoutTooltip(window, alone.stockPosition, stockSize, sections, c).fits)
      any = true;
  }
  if (!any)
    return;
  EXPECT_EQ(p.stockPosition, alone.stockPosition);
  for (size_t k = 0; k < sideIdx.size(); ++k) {
    EXPECT_EQ(p.sides[sideIdx[k]], alone.sides[k]);
    EXPECT_EQ(p.rects[sideIdx[k]], alone.rects[k]);
  }
}

}

TEST(TooltipPlacementTest, BelowPanelMatrix) {
  for (float scale : {1.0f, 1.5f, 2.0f}) {
    Vec2F window = windowAt(scale);
    int w = window[0], h = window[1];
    List<Vec2I> mice = {{5, h - 5}, {w - 5, h - 5}, {5, 5}, {w - 5, 5},
      {w / 2, h - 5}, {w / 2, 5}, {5, h / 2}, {w - 5, h / 2}, {w / 2, h / 2}};
    for (int width : {100, Stock[0], 260}) {
      for (bool withSide : {false, true}) {
        for (int tall : {0, 1, 2}) {  // nothing, the side panel, the below panel over-tall
          TooltipSection below{{TooltipSide::Below, TooltipSide::Above}, Vec2I(width, tall == 2 ? 3 * h : 80)};
          TooltipSection side{{TooltipSide::Right, TooltipSide::Left}, Vec2I(195, tall == 1 ? 3 * h : 300)};
          if (tall == 1 && !withSide)
            continue;
          List<TooltipSection> sections = withSide ? List<TooltipSection>{side, below} : List<TooltipSection>{below};
          for (auto mouse : mice) {
            auto p = placeTooltip(window, mouse, Offset, Stock, sections);
            std::string what = strf("scale {} mouse {} width {} side {} tall {}", scale, mouse, width, withSide, tall);
            checkPlacement(p, window, Stock, sections, what);
            checkSidePrecedence(p, window, mouse, Offset, Stock, sections, what);
            auto const& r = p.rects.last();
            RectI stock = stockRect(p);
            // directly below, or above when not
            if (p.sides.last() == TooltipSide::Below)
              EXPECT_EQ(r.yMax(), stock.yMin()) << what;
            else
              EXPECT_EQ(r.yMin(), stock.yMax()) << what;
            // the item panel stays at its stock position whenever some placement allows it
            Vec2I stock0 = stockAlone(window, mouse);
            EXPECT_EQ(p.stockPosition == stock0, fitsAt(window, stock0, Stock, sections)) << what;
            // an over-tall below panel fills the rest of the screen height
            if (tall == 2)
              EXPECT_EQ(r.height(), (int)std::floor(window[1]) - Stock[1]) << what;
          }
        }
      }
    }
  }
}

TEST(TooltipPlacementTest, InvertedL) {
  Vec2F window = windowAt(1);
  TooltipSection wide{{TooltipSide::Below, TooltipSide::Above}, Vec2I(260, 80)};
  // crafting right: processing shares the right edge and extends left
  auto p = placeTooltip(window, Vec2I(400, 800), Offset, Stock, {Crafting, wide});
  EXPECT_EQ(p.sides, (List<TooltipSide>{TooltipSide::Right, TooltipSide::Below}));
  EXPECT_EQ(p.rects[1].xMax(), stockRect(p).xMax());
  EXPECT_EQ(p.rects[1].yMax(), stockRect(p).yMin());
  // crafting left (no room on the right): shares the left edge, extends right
  p = placeTooltip(window, Vec2I(1640, 800), Offset, Stock, {Crafting, wide});
  EXPECT_EQ(p.sides[0], TooltipSide::Left);
  EXPECT_EQ(p.rects[1].xMin(), stockRect(p).xMin());
  // no side panel, near the right edge: extends left
  p = placeTooltip(window, Vec2I(1700, 800), Offset, Stock, {wide});
  EXPECT_EQ(p.rects[0].xMax(), stockRect(p).xMax());
  p = placeTooltip(window, Vec2I(400, 800), Offset, Stock, {wide});
  EXPECT_EQ(p.rects[0].xMin(), stockRect(p).xMin());
}

TEST(TooltipPlacementTest, ExtendsRightUnderAShortSidePanel) {
  Vec2F window = windowAt(1);
  TooltipSection shortSide{{TooltipSide::Right, TooltipSide::Left}, Vec2I(195, 80)};
  TooltipSection wide{{TooltipSide::Below, TooltipSide::Above}, Vec2I(260, 80)};
  for (int gap : {0, 2}) {
    TooltipSpacing spacing{RectI(0, 0, 0, 0), gap};
    auto p = placeTooltip(window, Vec2I(400, 800), Offset, Stock, {shortSide, wide}, {}, spacing);
    EXPECT_EQ(p.sides, (List<TooltipSide>{TooltipSide::Right, TooltipSide::Below}));
    EXPECT_EQ(p.rects[1].xMin(), stockRect(p).xMin());
    EXPECT_GT(p.rects[1].xMax(), stockRect(p).xMax());
    EXPECT_EQ(p.rects[1].yMax(), stockRect(p).yMin() - gap);
    EXPECT_GE(p.rects[0].yMin() - p.rects[1].yMax(), gap);
  }
}

TEST(TooltipPlacementTest, TallSidePanelForcesExtendLeft) {
  Vec2F window = windowAt(1);
  TooltipSection wide{{TooltipSide::Below, TooltipSide::Above}, Vec2I(260, 80)};
  auto p = placeTooltip(window, Vec2I(400, 800), Offset, Stock, {Crafting, wide});
  EXPECT_EQ(p.sides, (List<TooltipSide>{TooltipSide::Right, TooltipSide::Below}));
  EXPECT_EQ(p.rects[1].xMax(), stockRect(p).xMax());
  EXPECT_EQ(p.rects[0], placeTooltip(window, Vec2I(400, 800), Offset, Stock, {Crafting}).rects[0]);
}

TEST(TooltipPlacementTest, RightScreenEdgeForcesExtendLeft) {
  Vec2F window = windowAt(1);
  TooltipSection narrowSide{{TooltipSide::Right, TooltipSide::Left}, Vec2I(60, 80)};
  TooltipSection wide{{TooltipSide::Below, TooltipSide::Above}, Vec2I(260, 80)};
  auto p = placeTooltip(window, Vec2I(1655, 800), Offset, Stock, {narrowSide, wide});
  EXPECT_EQ(p.stockPosition, stockAlone(window, Vec2I(1655, 800)));
  EXPECT_EQ(p.sides, (List<TooltipSide>{TooltipSide::Right, TooltipSide::Below}));
  EXPECT_GT(stockRect(p).xMin() + 260, 1920);
  EXPECT_EQ(p.rects[1].xMax(), stockRect(p).xMax());
}

TEST(TooltipPlacementTest, SidePrecedenceIgnoresRegistrationOrder) {
  // the below panel registered first. Below with crafting right does not fit
  // (left of the screen, or slid under crafting); the below panel flips above
  // rather than pushing crafting to the left
  Vec2F window = windowAt(1);
  Vec2I mouse(200, 500);
  TooltipSection wide{{TooltipSide::Below, TooltipSide::Above}, Vec2I(400, 80)};
  auto p = placeTooltip(window, mouse, Offset, Stock, {wide, Crafting});
  EXPECT_EQ(p.sides, (List<TooltipSide>{TooltipSide::Above, TooltipSide::Right}));
  EXPECT_EQ(p.rects[1], placeTooltip(window, mouse, Offset, Stock, {Crafting}).rects[0]);
}

TEST(TooltipPlacementTest, FlipsAboveNearTheBottom) {
  Vec2F window = windowAt(1);
  // the item panel sits on the screen bottom: no room below
  auto p = placeTooltip(window, Vec2I(400, 130), Offset, Stock, {Crafting, Processing});
  EXPECT_EQ(p.stockPosition, stockAlone(window, Vec2I(400, 130)));
  EXPECT_EQ(p.stockPosition[1], 0);
  EXPECT_EQ(p.sides[1], TooltipSide::Above);
  EXPECT_EQ(p.rects[1].yMin(), stockRect(p).yMax());
}

TEST(TooltipPlacementTest, NeverUnderASidePanel) {
  // narrow screen: below the item panel, the processing panel would lie under
  // the taller crafting panel extending right, leave the screen extending left,
  // and lie under crafting again slid onto the screen, so it goes above,
  // sharing the left edge
  Vec2F window(700, 800);
  TooltipSection wide{{TooltipSide::Below, TooltipSide::Above}, Vec2I(300, 80)};
  Vec2I mouse(20, 700);
  auto p = placeTooltip(window, mouse, Offset, Stock, {Crafting, wide});
  checkPlacement(p, window, Stock, {Crafting, wide}, "no overlap");
  EXPECT_EQ(p.stockPosition, stockAlone(window, mouse));
  EXPECT_EQ(p.sides, (List<TooltipSide>{TooltipSide::Right, TooltipSide::Above}));
  EXPECT_EQ(p.rects[1].xMin(), stockRect(p).xMin());
  EXPECT_EQ(p.rects[1].yMin(), stockRect(p).yMax());
}

TEST(TooltipPlacementTest, WorstCaseMovesItemPanel) {
  // the screen is only just tall enough for the item and processing panels together
  Vec2F window(800, 300);
  TooltipSection below{{TooltipSide::Below, TooltipSide::Above}, Vec2I(195, 170)};
  for (Vec2I mouse : {Vec2I(100, 150), Vec2I(100, 200), Vec2I(700, 150)}) {
    auto p = placeTooltip(window, mouse, Offset, Stock, {Crafting, below});
    checkPlacement(p, window, Stock, {Crafting, below}, strf("{}", mouse));
    Vec2I stock = stockAlone(window, mouse);
    EXPECT_NE(p.stockPosition, stock);
    // only vertically, by the least amount: to the top (below) or bottom (above)
    EXPECT_EQ(p.stockPosition[0], stock[0]);
    // below fits from y 170 up, above up to y 10
    int need = std::min(std::max(0, 170 - stock[1]), std::max(0, stock[1] - 10));
    EXPECT_EQ(std::abs(p.stockPosition[1] - stock[1]), need) << mouse;
  }
}

TEST(TooltipPlacementTest, LatchedBelowAboveIsKept) {
  Vec2F window = windowAt(1);
  auto p = placeTooltip(window, Vec2I(400, 800), Offset, Stock, {Crafting, Processing},
      {TooltipSide::Right, TooltipSide::Above});
  EXPECT_EQ(p.sides[1], TooltipSide::Above);
}

TEST(TooltipPlacementTest, TwoPanelsTwoAugmentations) {
  // two side panels from two augmentations stack outwards; with a below panel all three pack
  Vec2F window = windowAt(1);
  TooltipSection narrow{{TooltipSide::Right, TooltipSide::Left}, Vec2I(120, 100)};
  List<TooltipSection> sections = {Crafting, narrow, Processing};
  for (Vec2I mouse : {Vec2I(400, 800), Vec2I(1800, 800), Vec2I(400, 100), Vec2I(1000, 1070)}) {
    auto p = placeTooltip(window, mouse, Offset, Stock, sections);
    checkPlacement(p, window, Stock, sections, strf("{}", mouse));
    EXPECT_EQ(p.stockPosition, stockAlone(window, mouse));
  }
}

// Random sizes and cursor positions on a small screen. Besides the shared
// assertions, a brute force over every stock position checks that the item
// panel stays put whenever some placement allows it and otherwise moves no
// further than needed.
TEST(TooltipPlacementTest, RandomizedProperty) {
  std::mt19937 rng(1234);
  auto uniform = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
  Vec2F window(240, 180);
  Vec2I offset(4, -4);
  int moved = 0, impossible = 0;
  for (int round = 0; round < 200; ++round) {
    Vec2I stockSize(uniform(30, 90), uniform(20, 70));
    List<TooltipSection> sections;
    if (uniform(0, 1))
      sections.append({{TooltipSide::Right, TooltipSide::Left}, Vec2I(uniform(20, 100), uniform(20, 260))});
    sections.append({{TooltipSide::Below, TooltipSide::Above}, Vec2I(uniform(20, 140), uniform(10, 200))});
    Vec2I mouse(uniform(0, 239), uniform(0, 179));
    auto p = placeTooltip(window, mouse, offset, stockSize, sections);

    // brute force with the same layout rule
    Vec2I stock0;
    stock0[0] = mouse[0] + offset[0] + stockSize[0] > 240 ? mouse[0] - offset[0] - stockSize[0] : mouse[0] + offset[0];
    stock0[1] = mouse[1] + offset[1] - stockSize[1] < 0 ? mouse[1] - offset[1] : mouse[1] + offset[1] - stockSize[1];
    stock0 = stock0.piecewiseMin(Vec2I(window) - stockSize).piecewiseMax(Vec2I());
    List<List<TooltipSide>> combos = {{}};
    for (auto const& s : sections) {
      List<List<TooltipSide>> next;
      for (auto const& c : combos)
        for (auto side : s.sides)
          next.append(c), next.last().append(side);
      combos = next;
    }
    Maybe<int> bestCost;
    for (int x = 0; x + stockSize[0] <= 240; ++x)
      for (int y = 0; y + stockSize[1] <= 180; ++y)
        for (auto const& c : combos)
          if (layoutTooltip(window, Vec2I(x, y), stockSize, sections, c).fits) {
            int cost = std::abs(x - stock0[0]) + std::abs(y - stock0[1]);
            if (!bestCost || cost < *bestCost)
              bestCost = cost;
          }
    std::string what = strf("round {} stock {} mouse {} sizes {} {}", round, stockSize, mouse,
        sections.first().size, sections.last().size);
    if (!bestCost) {
      ++impossible;
      continue;
    }
    if (*bestCost > 0)
      ++moved;
    checkPlacement(p, window, stockSize, sections, what);
    checkSidePrecedence(p, window, mouse, offset, stockSize, sections, what);
    int cost = std::abs(p.stockPosition[0] - stock0[0]) + std::abs(p.stockPosition[1] - stock0[1]);
    EXPECT_EQ(cost, *bestCost) << what;
  }
  // the cases must include ones where the item panel has to move
  EXPECT_GT(moved, 10) << moved << " moved, " << impossible << " impossible";
}

// vanilla-like art: 12px shadow on the right, 20 rows of shadow at the bottom
TEST(TooltipPlacementTest, GapBetweenVisibleEdges) {
  RectI margins(0, 20, 12, 0);
  for (int gap : {0, 1, 2}) {
    TooltipSpacing spacing{margins, gap};
    Vec2F window = windowAt(1);
    auto visible = [&](RectI const& art) { return RectI(art.min() + margins.min(), art.max() - margins.max()); };
    RectI stockVis = [&](Vec2I p) { return visible(RectI::withSize(p, Stock)); }(Vec2I());

    TooltipSection right{{TooltipSide::Right}, Vec2I(195, 300), margins};
    auto p = placeTooltip(window, Vec2I(400, 800), Offset, Stock, {right}, {}, spacing);
    stockVis = visible(RectI::withSize(p.stockPosition, Stock));
    EXPECT_EQ(visible(p.rects[0]).xMin(), stockVis.xMax() + gap);
    EXPECT_EQ(visible(p.rects[0]).yMax(), stockVis.yMax());

    TooltipSection left{{TooltipSide::Left}, Vec2I(195, 300), margins};
    p = placeTooltip(window, Vec2I(900, 800), Offset, Stock, {left}, {}, spacing);
    stockVis = visible(RectI::withSize(p.stockPosition, Stock));
    EXPECT_EQ(visible(p.rects[0]).xMax(), stockVis.xMin() - gap);

    TooltipSection below{{TooltipSide::Below}, Vec2I(195, 100), margins};
    p = placeTooltip(window, Vec2I(400, 800), Offset, Stock, {right, below}, {}, spacing);
    stockVis = visible(RectI::withSize(p.stockPosition, Stock));
    EXPECT_EQ(visible(p.rects[1]).yMax(), stockVis.yMin() - gap);
    EXPECT_EQ(visible(p.rects[1]).xMax(), stockVis.xMax());

    TooltipSection above{{TooltipSide::Above}, Vec2I(195, 100), margins};
    p = placeTooltip(window, Vec2I(400, 500), Offset, Stock, {above}, {}, spacing);
    stockVis = visible(RectI::withSize(p.stockPosition, Stock));
    EXPECT_EQ(visible(p.rects[0]).yMin(), stockVis.yMax() + gap);
    EXPECT_EQ(visible(p.rects[0]).xMin(), stockVis.xMin());
  }
}

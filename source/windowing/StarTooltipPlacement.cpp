#include "StarTooltipPlacement.hpp"

namespace Star {

static bool isVertical(TooltipSide side) {
  return side == TooltipSide::Below || side == TooltipSide::Above;
}

static bool onScreen(RectI const& rect, Vec2F const& screen) {
  return rect.xMin() >= 0 && rect.yMin() >= 0 && rect.xMax() <= screen[0] && rect.yMax() <= screen[1];
}

// Art and visible rects differ by margins (min: left, bottom; max: right, top).
static RectI visibleOf(RectI const& art, RectI const& m) {
  return RectI(art.min() + m.min(), art.max() - m.max());
}

TooltipLayout layoutTooltip(Vec2F const& screen, Vec2I const& stock, Vec2I const& stockSize,
    List<TooltipSection> const& sections, List<TooltipSide> const& sides, bool force, TooltipSpacing const& spacing) {
  TooltipLayout layout;
  int gap = spacing.gap;
  RectI stockRect = RectI::withSize(stock, stockSize);
  RectI stockVis = visibleOf(stockRect, spacing.stockMargins);
  layout.fits = onScreen(stockRect, screen);
  layout.rects.resize(sections.size());
  // visible rects so far, and whether each is a left/right section
  List<pair<RectI, bool>> placed = {{stockVis, false}};
  int left = stockVis.xMin();
  int right = stockVis.xMax();

  for (size_t i = 0; i < sections.size(); ++i) {
    TooltipSide side = sides[i];
    if (isVertical(side))
      continue;
    RectI const& m = sections[i].margins;
    Vec2I size(sections[i].size[0], std::min(sections[i].size[1], (int)std::floor(screen[1])));
    int top = stockVis.yMax() + m.yMax();
    int y = std::max(std::min(top - size[1], (int)std::floor(screen[1] - size[1])), 0);
    layout.unslid.append(RectI::withSize(Vec2I(0, top - size[1]), size));
    int x = side == TooltipSide::Right ? right + gap - m.xMin() : left - gap + m.xMax() - size[0];
    RectI rect = RectI::withSize(Vec2I(x, y), size);
    if (rect.xMin() < 0 || rect.xMax() > screen[0]) {
      layout.fits = false;
      if (force)
        rect.translate(Vec2I(std::max(std::min(x, (int)std::floor(screen[0] - size[0])), 0) - x, 0));
    }
    RectI vis = visibleOf(rect, m);
    if (side == TooltipSide::Right)
      right = vis.xMax();
    else
      left = vis.xMin();
    layout.rects[i] = rect;
    placed.append({vis, true});
  }

  int budget = std::floor(screen[1] - stockSize[1]);
  for (size_t i = 0; i < sections.size(); ++i) {
    TooltipSide side = sides[i];
    if (!isVertical(side))
      continue;
    RectI const& m = sections[i].margins;
    int w = sections[i].size[0];
    int h = std::max(std::min(sections[i].size[1], budget), 1);
    budget -= h;
    int leftX = stockVis.xMin() - m.xMin();
    int rightX = stockVis.xMax() + m.xMax() - w;

    // the rect at `x`, `gap` under (over) the stock panel or the below / above
    // panels it lies under (over)
    auto at = [&](int x) {
      int visMin = x + m.xMin(), visMax = x + w - m.xMax();
      int edge = side == TooltipSide::Below ? stockVis.yMin() : stockVis.yMax();
      for (auto const& [other, isSide] : placed) {
        if (isSide || other.xMin() >= visMax || other.xMax() <= visMin)
          continue;
        int e = side == TooltipSide::Below ? other.yMin() : other.yMax();
        if (side == TooltipSide::Below ? e < edge : e > edge)
          edge = e;
      }
      int y = side == TooltipSide::Below ? edge - gap + m.yMax() - h : edge + gap - m.yMin();
      return RectI::withSize(Vec2I(x, y), Vec2I(w, h));
    };
    // a left/right panel closer than `gap` is in the way
    auto clear = [&](RectI const& rect) {
      RectI vis = visibleOf(rect, m).padded(gap);
      for (auto const& [other, isSide] : placed) {
        if (isSide && vis.intersects(other, false))
          return false;
      }
      return true;
    };
    auto slide = [&](int x) { return std::max(std::min(x, (int)std::floor(screen[0] - w)), 0); };

    // share the left edge and extend right, else the right edge and extend
    // left, else slid onto the screen from either
    List<int> candidates = {leftX, rightX, slide(leftX), slide(rightX)};
    Maybe<int> chosen;
    for (int x : candidates) {
      RectI r = at(x);
      if (r.xMin() >= 0 && r.xMax() <= screen[0] && clear(r)) {
        chosen = x;
        break;
      }
    }
    int x = chosen.value(slide(leftX));
    RectI rect = at(x);
    layout.unslid.append(at(leftX));
    layout.unslid.append(at(rightX));
    if (!clear(rect))
      layout.fits = false;
    if (!onScreen(rect, screen)) {
      layout.fits = false;
      if (force)
        rect.translate(Vec2I(0, std::max(std::min(rect.yMin(), (int)std::floor(screen[1] - h)), 0) - rect.yMin()));
    }
    layout.rects[i] = rect;
    placed.append({visibleOf(rect, m), false});
  }
  return layout;
}

TooltipPlacement placeTooltip(Vec2F const& window, Vec2I const& mouse, Vec2I const& mouseOffset,
    Vec2I const& stockSize, List<TooltipSection> const& sections, List<TooltipSide> const& latched,
    TooltipSpacing const& spacing) {
  // kept fractional; positions round toward staying on screen
  Vec2F const& screen = window;

  // stock rules, by the stock panel's size alone
  Vec2I stock;
  if (mouse[0] + mouseOffset[0] + stockSize[0] > window[0])
    stock[0] = mouse[0] - mouseOffset[0] - stockSize[0];
  else
    stock[0] = mouse[0] + mouseOffset[0];
  if (mouse[1] + mouseOffset[1] - stockSize[1] < 0)
    stock[1] = mouse[1] - mouseOffset[1];
  else
    stock[1] = mouse[1] + mouseOffset[1] - stockSize[1];
  stock = stock.piecewiseMin(Vec2I::floor(screen - Vec2F(stockSize))).piecewiseMax(Vec2I());

  TooltipPlacement result;
  result.stockPosition = stock;
  if (sections.empty())
    return result;

  // a section is a side or a vertical panel by its first preference; side
  // panels take precedence (V14)
  auto vertical = [&](size_t i) { return isVertical(sections[i].sides.first()); };

  // side combinations in search order; each section's options are its
  // preference list with its latched side moved to the front; side panels
  // vary slowest, then sections in order
  List<size_t> order;
  for (size_t i = 0; i < sections.size(); ++i) {
    if (!vertical(i))
      order.append(i);
  }
  for (size_t i = 0; i < sections.size(); ++i) {
    if (vertical(i))
      order.append(i);
  }
  List<List<TooltipSide>> options;
  for (size_t i = 0; i < sections.size(); ++i) {
    List<TooltipSide> sides;
    if (latched.size() == sections.size() && sections[i].sides.contains(latched[i]))
      sides.append(latched[i]);
    for (auto side : sections[i].sides) {
      if (!sides.contains(side))
        sides.append(side);
    }
    options.append(std::move(sides));
  }
  List<List<TooltipSide>> combos = {List<TooltipSide>(sections.size(), TooltipSide::Left)};
  for (size_t i : order) {
    List<List<TooltipSide>> next;
    for (auto const& combo : combos) {
      for (auto side : options[i]) {
        next.append(combo);
        next.last()[i] = side;
      }
    }
    combos = std::move(next);
  }

  auto finish = [&](Vec2I const& position, List<TooltipSide> const& sides, TooltipLayout const& layout) {
    result.stockPosition = position;
    result.sides = sides;
    result.rects = layout.rects;
    return result;
  };

  // side panels first, placed as if there were no below / above panels; the
  // below / above panels then go wherever they fit around them
  List<size_t> sideIdx, vertIdx;
  for (size_t i = 0; i < sections.size(); ++i)
    (vertical(i) ? vertIdx : sideIdx).append(i);
  if (!sideIdx.empty() && !vertIdx.empty()) {
    List<TooltipSection> sideSections;
    List<TooltipSide> sideLatched;
    for (size_t i : sideIdx) {
      sideSections.append(sections[i]);
      if (latched.size() == sections.size())
        sideLatched.append(latched[i]);
    }
    auto alone = placeTooltip(window, mouse, mouseOffset, stockSize, sideSections, sideLatched, spacing);
    if (layoutTooltip(screen, alone.stockPosition, stockSize, sideSections, alone.sides, false, spacing).fits) {
      for (auto const& combo : combos) {
        bool same = true;
        for (size_t k = 0; k < sideIdx.size(); ++k)
          same = same && combo[sideIdx[k]] == alone.sides[k];
        if (!same)
          continue;
        auto layout = layoutTooltip(screen, alone.stockPosition, stockSize, sections, combo, false, spacing);
        if (layout.fits)
          return finish(alone.stockPosition, combo, layout);
      }
    }
  }

  List<TooltipLayout> atStock;
  for (auto const& combo : combos)
    atStock.append(layoutTooltip(screen, stock, stockSize, sections, combo, false, spacing));
  for (size_t c = 0; c < combos.size(); ++c) {
    if (atStock[c].fits)
      return finish(stock, combos[c], atStock[c]);
  }

  // moves that bring some rect's edge to a screen edge
  auto addUnique = [](List<int>& list, int value) {
    if (!list.contains(value))
      list.append(value);
  };
  List<int> dxs = {0}, dys = {0};
  for (auto const& layout : atStock) {
    List<RectI> rects = layout.rects;
    rects.appendAll(layout.unslid);
    for (auto const& rect : rects) {
      addUnique(dxs, -rect.xMin());
      addUnique(dxs, std::floor(screen[0] - rect.xMax()));
      addUnique(dys, -rect.yMin());
      addUnique(dys, std::floor(screen[1] - rect.yMax()));
    }
  }
  struct Best { int cost; size_t combo; Vec2I position; TooltipLayout layout; };
  Maybe<Best> best;
  for (int dx : dxs) {
    for (int dy : dys) {
      Vec2I position = stock + Vec2I(dx, dy);
      if (position[0] < 0 || position[1] < 0 || position[0] + stockSize[0] > screen[0] || position[1] + stockSize[1] > screen[1])
        continue;
      int cost = std::abs(dx) + std::abs(dy);
      for (size_t c = 0; c < combos.size(); ++c) {
        auto layout = layoutTooltip(screen, position, stockSize, sections, combos[c], false, spacing);
        if (!layout.fits)
          continue;
        if (!best || cost < best->cost || (cost == best->cost && c < best->combo))
          best = Best{cost, c, position, std::move(layout)};
      }
    }
  }
  if (best)
    return finish(best->position, combos[best->combo], best->layout);

  return finish(stock, combos[0], layoutTooltip(screen, stock, stockSize, sections, combos[0], true, spacing));
}

int tooltipScrollOffset(float elapsed, int contentHeight, int visibleHeight, float hold, float speed) {
  int range = contentHeight - visibleHeight;
  if (range <= 0 || elapsed <= 0 || speed <= 0)
    return 0;
  hold = std::max(hold, 0.0f);
  float travel = range / speed;
  float cycle = 2 * hold + travel;
  float t = std::fmod(elapsed, cycle);
  if (t < hold)
    return 0;
  if (t < hold + travel)
    return std::min(range, (int)std::floor((t - hold) * speed));
  return range;
}

}

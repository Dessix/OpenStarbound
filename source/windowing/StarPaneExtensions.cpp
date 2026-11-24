#include "StarPaneExtensions.hpp"
#include "StarRoot.hpp"
#include "StarAssets.hpp"
#include "StarGuiContext.hpp"

namespace Star {

PaneExtensions::PaneExtensions(Pane* pane) : m_pane(pane) {}

void PaneExtensions::setTooltipInfo(String kind, ItemPtr item) {
  m_tooltipKind = std::move(kind);
  m_tooltipItem = std::move(item);
}

String const& PaneExtensions::tooltipKind() const {
  return m_tooltipKind;
}

ItemPtr const& PaneExtensions::tooltipItem() const {
  return m_tooltipItem;
}

int PaneExtensions::stripSize() const {
  int total = 0;
  for (auto const& extension : m_extensions) {
    if (!extension.side)
      total += extension.size;
  }
  return total;
}

int PaneExtensions::sectionSize(TooltipSide side) const {
  int total = 0;
  for (auto const& extension : m_extensions) {
    if (extension.side && *extension.side == side)
      total += extension.size;
  }
  return total;
}

bool PaneExtensions::hasSections() const {
  return m_extensions.any([](Extension const& extension) { return (bool)extension.side; });
}

// Pane art usually has a drop shadow or transparent margin around the visible
// panel. Measured once per image and pane, so that the art can be drawn at
// other widths with its corners, border and shadow intact.
PaneExtensions::PanelArt const& PaneExtensions::panelArt(String const& image) const {
  if (auto art = m_panelArt.ptr(image))
    return *art;
  PanelArt art;
  if (!image.empty()) {
    auto pixels = Root::singleton().assets()->image(image);
    int width = pixels->width();
    int height = pixels->height();
    art.width = width;
    art.height = height;
    art.right = width;
    if (width > 0 && height > 0) {
      auto opaque = [&](int x, int y) { return pixels->get(x, y)[3] >= 192; };
      auto opaqueColumn = [&](int x) {
        for (int y = 0; y < height; ++y) {
          if (opaque(x, y))
            return true;
        }
        return false;
      };
      auto opaqueRow = [&](int y) {
        for (int x = 0; x < width; ++x) {
          if (opaque(x, y))
            return true;
        }
        return false;
      };
      while (art.left < width - 1 && !opaqueColumn(art.left))
        ++art.left;
      while (art.right > art.left + 1 && !opaqueColumn(art.right - 1))
        --art.right;
      while (art.bottom < height - 1 && !opaqueRow(art.bottom))
        ++art.bottom;
      while (art.top < height - 1 - art.bottom && !opaqueRow(height - 1 - art.top))
        ++art.top;

      // the column to stretch: one equal to its neighbour in every row, as
      // close to the middle of the panel as possible
      auto plain = [&](int x) {
        for (int y = 0; y < height; ++y) {
          if (pixels->get(x, y) != pixels->get(x + 1, y))
            return false;
        }
        return true;
      };
      int middle = (art.left + art.right) / 2;
      art.split = clamp(middle, 0, width - 1);
      for (int d = 0; d < width; ++d) {
        int found = -1;
        for (int x : {middle - d, middle + d}) {
          if (x > art.left && x + 1 < art.right - 1 && plain(x)) {
            found = x;
            break;
          }
        }
        if (found >= 0) {
          art.split = found;
          break;
        }
        if (middle - d <= art.left && middle + d >= art.right - 2)
          break;
      }

      int middleRow = height / 2;
      int fewest = width + 1;
      for (int y = 0; y < height; ++y) {
        int unlike = 0;
        for (int x = art.left; x < art.right; ++x)
          unlike += pixels->get(x, y) != pixels->get(art.split, y);
        if (unlike < fewest || (unlike == fewest && std::abs(y - middleRow) < std::abs(art.plainRow - middleRow))) {
          fewest = unlike;
          art.plainRow = y;
        }
      }
    }
  }
  return m_panelArt.add(image, art);
}

Pane::BGResult PaneExtensions::extensionImages(Extension const& extension) const {
  return {
    extension.images.header.empty() ? m_pane->m_bgHeader : extension.images.header,
    extension.images.body.empty() ? m_pane->m_bgBody : extension.images.body,
    extension.images.footer.empty() ? m_pane->m_bgFooter : extension.images.footer
  };
}

// The image whose opaque columns give a section's visible panel.
static String const& referenceImage(Pane::BGResult const& images) {
  return !images.body.empty() ? images.body : !images.header.empty() ? images.header : images.footer;
}

RectI PaneExtensions::extendBackground(Extension const& extension, WidgetPtr const& content) {
  if (extension.size <= 0)
    throw GuiException("Pane extension size must be positive");
  if (extension.height && *extension.height <= 0)
    throw GuiException("Pane extension height must be positive");
  if (m_extensions.empty()) {
    m_baseSize = m_pane->size();
    m_stockOffset = Vec2I();
  }

  if (!extension.side) {
    if (!extension.sides.empty() || extension.height)
      throw GuiException("Bottom pane extensions take no sides or height");
    for (auto const& member : m_pane->m_members) {
      if (member->relativePosition()[1] >= m_stockOffset[1] + m_pane->m_footerSize[1])
        member->setPosition(member->relativePosition() + Vec2I(0, extension.size));
    }
    m_extensions.append(extension);
    layoutExtensions();
    int panelLeft = m_pane->m_bgBody.empty() ? 0 : panelArt(m_pane->m_bgBody).left;
    return RectI::withSize(m_stockOffset + Vec2I(panelLeft, m_pane->m_footerSize[1]), Vec2I(stockJoin() - panelLeft, extension.size));
  }

  Extension section = extension;
  if (section.sides.empty())
    section.sides = {*section.side};
  section.side = section.sides.first();
  section.content = content;
  section.placed = {};
  section.scroll = 0;
  auto const& art = panelArt(referenceImage(extensionImages(section)));
  section.artMargins = Vec2I(art.left, art.width - art.right);
  m_extensions.append(std::move(section));
  layoutExtensions();
  return m_extensions.last().rect;
}

int PaneExtensions::stockJoin() const {
  auto bg = m_pane->getBG();
  String const& image = !bg.header.empty() ? bg.header : bg.body;
  if (image.empty())
    return m_extensions.empty() ? m_pane->size()[0] : m_baseSize[0];
  return panelArt(image).right;
}

RectI PaneExtensions::stockMargins() const {
  auto bg = m_pane->getBG();
  String const& image = !bg.header.empty() ? bg.header : bg.body;
  int left = image.empty() ? 0 : panelArt(image).left;
  int right = stockSize()[0] - stockJoin();
  String const& bottomImage = !bg.footer.empty() ? bg.footer : bg.body;
  String const& topImage = !bg.header.empty() ? bg.header : bg.body;
  int bottom = bottomImage.empty() ? 0 : panelArt(bottomImage).bottom;
  int top = topImage.empty() ? 0 : panelArt(topImage).top;
  return RectI(left, bottom, right, top);
}

RectI PaneExtensions::extensionMargins(Extension const& extension) const {
  auto images = extensionImages(extension);
  return RectI(extension.artMargins[0], images.footer.empty() ? 0 : panelArt(images.footer).bottom,
      extension.artMargins[1], images.header.empty() ? 0 : panelArt(images.header).top);
}

Vec2I PaneExtensions::stockSize() const {
  if (m_extensions.empty())
    return m_pane->size();
  return m_baseSize + Vec2I(0, stripSize());
}

Vec2I PaneExtensions::stockOffset() const {
  return m_stockOffset;
}

List<PaneExtensions::Extension> const& PaneExtensions::extensions() const {
  return m_extensions;
}

void PaneExtensions::placeExtensions(List<pair<TooltipSide, RectI>> const& placements) {
  size_t i = 0;
  bool changed = false;
  for (auto& extension : m_extensions) {
    if (!extension.side || i >= placements.size())
      continue;
    auto const& placement = placements[i++];
    if (!extension.sides.contains(placement.first))
      throw GuiException("Pane extension placed on a side it does not allow");
    if (*extension.side != placement.first || extension.placed != placement.second) {
      extension.side = placement.first;
      extension.placed = placement.second;
      changed = true;
    }
  }
  if (changed)
    layoutExtensions();
}

void PaneExtensions::setExtensionScroll(List<int> const& offsets) {
  size_t i = 0;
  for (auto& extension : m_extensions) {
    if (!extension.side)
      continue;
    extension.scroll = i < offsets.size() ? clamp(offsets[i], 0, std::max(extension.contentHeight - extension.rect.height(), 0)) : 0;
    ++i;
  }
}

// Lays the panels out around the stock panel: where PaneManager placed them,
// or else outwards from its art edges (below / above: left aligned), each at its own height. The pane
// is the bounding box; the stock content moves inside it.
void PaneExtensions::layoutExtensions() {
  Vec2I stock = stockSize();
  int left = 0;
  int right = stock[0];
  int below = 0;
  int above = stock[1];
  RectI bounds = RectI::withSize(Vec2I(), stock);
  for (auto& extension : m_extensions) {
    if (!extension.side)
      continue;
    TooltipSide side = *extension.side;
    int height = extension.height.value(stock[1]);
    int artWidth = extension.size + extension.artMargins[0] + extension.artMargins[1];
    RectI art;
    if (extension.placed) {
      art = *extension.placed;
    } else if (side == TooltipSide::Below) {
      art = RectI::withSize(Vec2I(0, below - height), Vec2I(artWidth, height));
    } else if (side == TooltipSide::Above) {
      art = RectI::withSize(Vec2I(0, above), Vec2I(artWidth, height));
    } else {
      int x = side == TooltipSide::Right ? right : left - artWidth;
      art = RectI::withSize(Vec2I(x, stock[1] - height), Vec2I(artWidth, height));
    }
    if (side == TooltipSide::Right)
      right = std::max(right, art.xMax());
    else if (side == TooltipSide::Left)
      left = std::min(left, art.xMin());
    else if (side == TooltipSide::Below)
      below = std::min(below, art.yMin());
    else
      above = std::max(above, art.yMax());
    extension.contentHeight = height;
    extension.artRect = art;
    bounds.combine(art);
  }

  Vec2I offset = -bounds.min();
  Vec2I delta = offset - m_stockOffset;
  if (delta != Vec2I()) {
    for (auto const& member : m_pane->m_members) {
      bool isSection = m_extensions.any([&](Extension const& extension) { return extension.content && extension.content == member; });
      if (!isSection)
        member->setPosition(member->relativePosition() + delta);
    }
  }
  m_stockOffset = offset;
  for (auto& extension : m_extensions) {
    if (!extension.side)
      continue;
    extension.artRect.translate(offset);
    extension.rect = RectI(extension.artRect.xMin() + extension.artMargins[0], extension.artRect.yMin(),
        extension.artRect.xMax() - extension.artMargins[1], extension.artRect.yMax());
    extension.scroll = clamp(extension.scroll, 0, std::max(extension.contentHeight - extension.rect.height(), 0));
    if (extension.content) {
      // the content keeps its full height, top aligned with the panel's top; a
      // panel shorter than its content shows part of it (see drawChildren)
      extension.content->setPosition(Vec2I(extension.rect.xMin(), extension.rect.yMax() - extension.contentHeight));
      extension.content->setSize(Vec2I(extension.rect.width(), extension.contentHeight));
    }
  }
  m_pane->setSize(bounds.size());
}

// Draws rows [rowMin, rowMax) of an image `width` wide starting at x, keeping
// its edges (corners, border, shadow) and stretching or dropping the plain
// column in the middle.
static void drawPanelSlice(GuiContext* context, String const& image, PaneExtensions::PanelArt const& art,
    float rowMin, float rowMax, float x, float yMin, float yMax, float width) {
  if (image.empty() || yMax <= yMin || width <= 0 || art.width <= 0)
    return;
  auto draw = [&](float texMin, float texMax, float destMin, float destMax) {
    if (destMax > destMin && texMax > texMin)
      context->drawInterfaceQuad(image, RectF(texMin, rowMin, texMax, rowMax), RectF(destMin, yMin, destMax, yMax));
  };
  float w = art.width;
  if (width >= w) {
    float s = art.split;
    draw(0, s, x, x + s);
    draw(s, s + 1, x + s, x + width - (w - s - 1));
    draw(s + 1, w, x + width - (w - s - 1), x + width);
  } else {
    float a = std::min<float>(art.split, std::floor(width / 2));
    draw(0, a, x, x + a);
    draw(w - (width - a), w, x + a, x + width);
  }
}

// Shadows fall right and down, so the panels left of or above the stock panel
// are drawn before it (outermost first) and the rest after it: no shadow is
// drawn over a neighbouring panel.
void PaneExtensions::render(bool beforeStock) {
  if (m_extensions.empty())
    return;
  GuiContext* context = m_pane->m_context;
  Vec2F origin = Vec2F(m_pane->position());
  Vec2F stockOrigin = Vec2F(m_pane->position() + m_stockOffset);

  // strips, stacked upwards from the footer in the order added
  int bottom = m_pane->m_footerSize[1];
  for (auto const& extension : m_extensions) {
    if (beforeStock || extension.side)
      continue;
    String const& body = extension.images.body.empty() ? m_pane->m_bgBody : extension.images.body;
    if (!body.empty()) {
      auto const& art = panelArt(body);
      int row = clamp(extension.bodyRow.value(art.height / 2), 0, std::max(art.height - 1, 0));
      context->drawInterfaceQuad(body, RectF(0, row, art.width, row + 1),
          RectF(stockOrigin[0], stockOrigin[1] + bottom, stockOrigin[0] + art.width, stockOrigin[1] + bottom + extension.size));
    }
    bottom += extension.size;
  }

  // sections, each a complete panel in the art, shadow included
  RectI stockVis(m_stockOffset + stockMargins().min(), m_stockOffset + stockSize() - stockMargins().max());
  for (size_t n = 0; n < m_extensions.size(); ++n) {
    auto const& extension = m_extensions[beforeStock ? m_extensions.size() - 1 - n : n];
    if (!extension.side)
      continue;
    bool before = extension.rect.xMax() <= stockVis.xMin() || extension.rect.yMin() + extensionMargins(extension).yMin() >= stockVis.yMax();
    if (before != beforeStock)
      continue;
    RectF rect = RectF(extension.artRect).translated(origin);
    // the visible panel; each image adds its own margins, so borders line up
    // even when header, body and footer have differently sized shadows
    float visMin = rect.xMin() + extension.artMargins[0];
    float visMax = rect.xMax() - extension.artMargins[1];
    auto images = extensionImages(extension);
    auto const& headerArt = panelArt(images.header);
    auto const& bodyArt = panelArt(images.body);
    auto const& footerArt = panelArt(images.footer);
    auto slice = [&](String const& image, PanelArt const& art, float rowMin, float rowMax, float yMin, float yMax) {
      drawPanelSlice(context, image, art, rowMin, rowMax, visMin - art.left, yMin, yMax, visMax - visMin + art.left + (art.width - art.right));
    };
    float footerHeight = std::min<float>(footerArt.height, rect.height());
    float headerHeight = std::min<float>(headerArt.height, rect.height() - footerHeight);
    int bodyRow = clamp(extension.bodyRow.value(bodyArt.plainRow), 0, std::max(bodyArt.height - 1, 0));
    slice(images.footer, footerArt, footerArt.height - footerHeight, footerArt.height, rect.yMin(), rect.yMin() + footerHeight);
    slice(images.body, bodyArt, bodyRow, bodyRow + 1, rect.yMin() + footerHeight, rect.yMax() - headerHeight);
    slice(images.header, headerArt, 0, headerHeight, rect.yMax() - headerHeight, rect.yMax());
  }
}

// Section content taller than its panel is drawn twice: the part under the
// header band stays put, the rest is clipped to the panel below the band and
// shifted by the section's scroll offset. A dim ellipsis marks each edge with
// hidden content. Without such a section, the pane draws its children as usual.
bool PaneExtensions::drawChildren() {
  auto clipped = [](Extension const& extension) {
    return extension.side && extension.content && extension.contentHeight > extension.rect.height();
  };
  if (!m_extensions.any(clipped))
    return false;

  GuiContext* context = m_pane->m_context;
  RectI drawingArea = m_pane->m_drawingArea;
  for (auto const& child : m_pane->m_members) {
    Extension const* section = nullptr;
    for (auto const& extension : m_extensions) {
      if (extension.side && extension.content && extension.content == child)
        section = &extension;
    }
    if (!section || section->contentHeight <= section->rect.height()) {
      child->render(drawingArea);
      continue;
    }

    auto images = extensionImages(*section);
    RectI rect = section->rect.translated(m_pane->screenPosition());
    int headerHeight = std::min(panelArt(images.header).height, rect.height());
    int footerShadow = std::min(panelArt(images.footer).bottom, rect.height() - headerHeight);
    int bandBottom = rect.yMax() - headerHeight;

    child->setDrawingOffset(Vec2I());
    child->render(drawingArea.limited(RectI(rect.xMin(), bandBottom, rect.xMax(), rect.yMax())));
    child->setDrawingOffset(Vec2I(0, section->scroll));
    child->render(drawingArea.limited(RectI(rect.xMin(), rect.yMin() + footerShadow, rect.xMax(), bandBottom)));
    child->setDrawingOffset(Vec2I());

    int hidden = section->contentHeight - rect.height();
    context->setTextStyle(m_pane->m_textStyle);
    context->setFontColor(Vec4B(255, 255, 255, 110));
    context->setFontMode(FontMode::Normal);
    float middle = (rect.xMin() + rect.xMax()) / 2.0f;
    if (section->scroll > 0)
      context->renderInterfaceText("…", TextPositioning(Vec2F(middle, bandBottom), HorizontalAnchor::HMidAnchor, VerticalAnchor::TopAnchor));
    if (section->scroll < hidden)
      context->renderInterfaceText("…", TextPositioning(Vec2F(middle, rect.yMin() + footerShadow), HorizontalAnchor::HMidAnchor, VerticalAnchor::BottomAnchor));
    context->clearTextStyle();
  }
  return true;
}

}

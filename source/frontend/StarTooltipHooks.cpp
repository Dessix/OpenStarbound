#include "StarTooltipHooks.hpp"
#include "StarRoot.hpp"
#include "StarAssets.hpp"
#include "StarJsonExtra.hpp"
#include "StarLuaConverters.hpp"
#include "StarPaneExtensions.hpp"
#include "StarGuiReader.hpp"
#include "StarWidgetLuaBindings.hpp"
#include "StarConfigLuaBindings.hpp"
#include "StarPlayerLuaBindings.hpp"
#include "StarUniverseClient.hpp"
#include "StarPlayer.hpp"
#include "StarItem.hpp"
#include "StarLabelWidget.hpp"
#include "StarImageWidget.hpp"
#include "StarImageStretchWidget.hpp"
#include "StarListWidget.hpp"
#include "StarItemSlotWidget.hpp"
#include "StarCanvasWidget.hpp"
#include "StarButtonWidget.hpp"

namespace Star {

size_t const TooltipCacheSize = 128;

// State of one augmentation call. Callbacks handed to Lua check `valid`, so a
// table stashed by a script raises an error instead of touching a pane that
// has since been destroyed.
struct TooltipHooks::Build {
  bool valid = true;
  bool replayable = true;
  PanePtr tooltip;
  String namePrefix;
  size_t counter = 0;
  List<Op> ops;
  GuiReaderPtr reader = make_shared<GuiReader>();

  void check() const {
    if (!valid)
      throw StarException("Tooltip callbacks used outside of the augmentation call");
  }

  String nextName() {
    return strf("{}{}", namePrefix, ++counter);
  }
};

static String widgetType(WidgetPtr const& widget) {
  if (is<LabelWidget>(widget))
    return "label";
  if (is<ImageWidget>(widget))
    return "image";
  if (is<ImageStretchWidget>(widget))
    return "imageStretch";
  if (is<ListWidget>(widget))
    return "list";
  if (is<ItemSlotWidget>(widget))
    return "itemslot";
  if (is<CanvasWidget>(widget))
    return "canvas";
  if (is<ButtonWidget>(widget))
    return "button";
  return "widget";
}

static JsonArray describeChildren(Widget* parent) {
  JsonArray children;
  for (size_t i = 0; i < parent->numChildren(); ++i) {
    auto child = parent->getChildNum(i);
    JsonObject desc{
      {"name", child->name()},
      {"type", widgetType(child)},
      {"position", jsonFromVec2I(child->relativePosition())},
      {"size", jsonFromVec2I(child->size())},
      {"visible", child->visibility()}
    };
    if (auto label = as<LabelWidget>(child))
      desc["text"] = label->text();
    if (child->numChildren() > 0)
      desc["children"] = describeChildren(child.get());
    children.append(std::move(desc));
  }
  return children;
}

static Json describeLayout(PanePtr const& tooltip) {
  auto& context = GuiContext::singleton();
  auto bg = tooltip->getBG();
  auto extensions = tooltip->findExtensions();
  auto sectionSize = [&](TooltipSide side) { return extensions ? extensions->sectionSize(side) : 0; };
  auto image = [&](String const& path) -> Json {
    if (path.empty())
      return Json();
    return JsonObject{{"image", path}, {"size", jsonFromVec2U(context.textureSize(path))}};
  };
  return JsonObject{
    {"size", jsonFromVec2I(tooltip->size())},
    {"background", JsonObject{{"header", image(bg.header)}, {"body", image(bg.body)}, {"footer", image(bg.footer)}}},
    {"extensions", JsonObject{
      {"bottom", extensions ? extensions->stripSize() : 0},
      {"left", sectionSize(TooltipSide::Left)},
      {"right", sectionSize(TooltipSide::Right)},
      {"below", sectionSize(TooltipSide::Below)},
      {"above", sectionSize(TooltipSide::Above)}}},
    {"widgets", describeChildren(tooltip.get())}
  };
}

// Identifies the stock layout and text an augmentation ran against, so a
// cached result is never replayed onto a different one. Text is included
// because tooltips without an item (simple text tooltips) share one cache key,
// and stock text such as a recipe's ingredient counts can change for an item.
static void hashWidgets(size_t& signature, Widget* parent) {
  for (size_t i = 0; i < parent->numChildren(); ++i) {
    auto child = parent->getChildNum(i);
    hashCombine(signature, hash<String>()(child->name()));
    hashCombine(signature, hash<Vec2I>()(child->relativePosition()));
    hashCombine(signature, hash<Vec2I>()(child->size()));
    if (auto label = as<LabelWidget>(child))
      hashCombine(signature, hash<String>()(label->text()));
    hashWidgets(signature, child.get());
  }
}

static size_t layoutSignature(PanePtr const& tooltip) {
  size_t signature = 0;
  auto bg = tooltip->getBG();
  hashCombine(signature, hash<String>()(bg.header + "|" + bg.body + "|" + bg.footer));
  hashCombine(signature, hash<Vec2I>()(tooltip->size()));
  hashWidgets(signature, tooltip.get());
  return signature;
}

TooltipHooks::TooltipHooks(UniverseClientPtr client) : m_client(std::move(client)), m_dirty(true), m_player(nullptr) {
  m_reloadListener = make_shared<CallbackListener>([this]() { m_dirty = true; });
  Root::singleton().registerReloadListener(m_reloadListener);
}

TooltipHooks::~TooltipHooks() {
  stop();
}

bool TooltipHooks::reloadIfNeeded() {
  // player callbacks are bound to one player; restart the scripts on a swap
  if (!m_scripts.empty() && m_client->mainPlayer().get() != m_player)
    m_dirty = true;
  if (!m_dirty)
    return false;
  m_dirty = false;
  stop();
  m_player = m_client->mainPlayer().get();

  auto scripts = Root::singleton().assets()->json("/client.config").opt("tooltipScripts");
  if (!scripts || !scripts->isType(Json::Type::Array))
    return true;

  for (auto const& path : scripts->iterateArray()) {
    if (!path.isType(Json::Type::String))
      continue;
    size_t scriptIndex = m_scripts.size();
    auto component = make_shared<LuaUpdatableComponent<LuaBaseComponent>>();
    component->setLuaRoot(m_client->luaRoot());
    component->setScript(path.toString());
    component->setUpdateDelta(0);
    component->addCallbacks("player", LuaBindings::makePlayerCallbacks(m_client->mainPlayer().get()));

    LuaCallbacks tooltips;
    tooltips.registerCallback("register", [this, scriptIndex](String const& name, LuaFunction build, Maybe<Json> const& options) {
      if (m_initializingScript != scriptIndex)
        throw StarException("tooltips.register may only be called during script init");
      Maybe<StringSet> kinds;
      if (options && options->contains("kinds"))
        kinds = jsonToStringSet(options->get("kinds"));
      m_augmentations.filter([&](Augmentation const& a) { return a.name != name; });
      auto& augmentation = m_augmentations.emplaceAppend();
      augmentation.name = name;
      augmentation.script = scriptIndex;
      augmentation.build = std::move(build);
      augmentation.kinds = std::move(kinds);
      augmentation.cache.setMaxSize(TooltipCacheSize);
    });
    component->addCallbacks("tooltips", std::move(tooltips));

    m_scripts.append({path.toString(), component});
    // init() logs its own errors; a script that fails to load registers nothing.
    m_initializingScript = scriptIndex;
    component->init();
    m_initializingScript.reset();
  }
  // a script that failed after registering must not be called
  m_augmentations.filter([this](Augmentation const& a) { return m_scripts[a.script].component->initialized(); });
  return true;
}

bool TooltipHooks::enabled() const {
  return !m_augmentations.empty();
}

void TooltipHooks::stop() {
  m_augmentations.clear();
  for (auto& script : m_scripts)
    script.component->uninit();
  m_scripts.clear();
}

void TooltipHooks::update(float dt) {
  for (auto& script : m_scripts) {
    if (script.component->updateDelta() > 0 && script.component->initialized())
      script.component->update(script.component->updateDt(dt));
  }
}

bool TooltipHooks::clearCache(String const& name, Maybe<String> const& itemName) {
  bool found = false;
  for (auto& augmentation : m_augmentations) {
    if (augmentation.name != name)
      continue;
    found = true;
    if (itemName)
      augmentation.cache.removeWhere([&](String const&, CacheEntry& entry) { return entry.itemName == *itemName; });
    else
      augmentation.cache.clear();
  }
  return found;
}

void TooltipHooks::logError(Augmentation& augmentation, String const& message) {
  if (augmentation.errorLogged)
    return;
  augmentation.errorLogged = true;
  Logger::error("Tooltip augmentation '{}' ({}) failed, further errors are not logged: {}",
      augmentation.name, m_scripts[augmentation.script].path, message);
}

static String tooltipKind(PanePtr const& tooltip) {
  auto extensions = tooltip->findExtensions();
  return extensions && !extensions->tooltipKind().empty() ? extensions->tooltipKind() : String("other");
}

static ItemPtr tooltipItem(PanePtr const& tooltip) {
  auto extensions = tooltip->findExtensions();
  return extensions ? extensions->tooltipItem() : ItemPtr();
}

bool TooltipHooks::refreshAllowed() {
  if (!m_building)
    return true;
  if (!m_building->refreshLogged) {
    m_building->refreshLogged = true;
    Logger::warn("Tooltip augmentation '{}' ({}) requested a tooltip refresh while building, ignored",
        m_building->name, m_scripts[m_building->script].path);
  }
  return false;
}

PanePtr TooltipHooks::apply(PanePtr tooltip, function<PanePtr()> const& rebuild) {
  String kind = tooltipKind(tooltip);
  auto item = tooltipItem(tooltip);
  String itemName = item ? item->name() : String();
  Json parameters = item ? item->parameters() : Json();
  String key = kind + "\x1f" + itemName;
  if (item)
    key += strf("\x1f{}\x1f{}", item->count(), hash<Json>()(parameters));

  // Results applied so far, replayed onto a fresh stock tooltip if a later
  // augmentation fails part way through.
  List<pair<Augmentation*, List<Op>>> applied;
  bool recoverable = true;

  for (auto& augmentation : m_augmentations) {
    if (augmentation.kinds && !augmentation.kinds->contains(kind))
      continue;

    size_t layout = layoutSignature(tooltip);
    List<Op> ops;
    bool replayable = true;
    bool ok;
    auto entry = augmentation.cache.ptr(key);
    if (entry && entry->layout == layout && entry->parameters == parameters) {
      ops = entry->ops;
      ok = replay(augmentation, tooltip, ops);
      if (!ok)
        augmentation.cache.remove(key);
    } else {
      ok = run(augmentation, tooltip, ops, replayable);
      if (ok && replayable)
        augmentation.cache.set(key, CacheEntry{itemName, parameters, layout, ops});
      else
        augmentation.cache.remove(key);
    }

    if (ok) {
      if (replayable)
        applied.append({&augmentation, std::move(ops)});
      else
        recoverable = false;
      continue;
    }

    tooltip = rebuild();
    if (!tooltip || !recoverable)
      return tooltip;
    for (auto const& p : applied) {
      if (!replay(*p.first, tooltip, p.second))
        return rebuild();
    }
  }

  return tooltip;
}

bool TooltipHooks::run(Augmentation& augmentation, PanePtr const& tooltip, List<Op>& ops, bool& replayable) {
  auto build = make_shared<Build>();
  build->tooltip = tooltip;
  build->namePrefix = augmentation.name + ".";

  bool ok = true;
  m_building = &augmentation;
  try {
    auto widgets = recordCallbacks(LuaBindings::makeWidgetCallbacks(tooltip.get(), build->reader), "widget", build);
    augmentation.build->invoke(makeTooltipCallbacks(build), widgets);
  } catch (std::exception const& e) {
    logError(augmentation, printException(e, false));
    ok = false;
  }
  m_building = nullptr;

  build->valid = false;
  build->tooltip.reset();
  ops = std::move(build->ops);
  replayable = build->replayable;
  return ok;
}

bool TooltipHooks::replay(Augmentation& augmentation, PanePtr const& tooltip, List<Op> const& ops) {
  try {
    auto& engine = m_scripts[augmentation.script].component->luaRoot()->luaEngine();
    auto reader = make_shared<GuiReader>();
    StringMap<LuaCallbacks> targets;
    for (auto const& op : ops) {
      if (op.target == "tooltip") {
        applyTooltipOp(tooltip, op.name, op.args);
        continue;
      }

      if (!targets.contains(op.target)) {
        Widget* widget = tooltip.get();
        if (op.target != "widget") {
          widget = tooltip->fetchChild(op.target).get();
          if (!widget)
            throw StarException(strf("No widget '{}' to replay onto", op.target));
        }
        targets[op.target] = LuaBindings::makeWidgetCallbacks(widget, reader);
      }

      auto function = targets[op.target].callbacks().ptr(op.name);
      if (!function)
        throw StarException(strf("No widget callback '{}'", op.name));
      LuaVariadic<LuaValue> args;
      for (auto const& arg : op.args)
        args.append(engine.luaFrom(arg));
      (*function)(engine, args.size(), args.ptr());
    }
    return true;
  } catch (std::exception const& e) {
    logError(augmentation, strf("while replaying cached result: {}", printException(e, false)));
    return false;
  }
}

Json TooltipHooks::applyTooltipOp(PanePtr const& tooltip, String const& name, JsonArray const& args) {
  if (name == "extend") {
    Json spec = args.at(0);
    // none: a bottom strip
    auto parseSide = [](String const& side) -> Maybe<TooltipSide> {
      if (side == "bottom")
        return {};
      else if (side == "left")
        return TooltipSide::Left;
      else if (side == "right")
        return TooltipSide::Right;
      else if (side == "below")
        return TooltipSide::Below;
      else if (side == "above")
        return TooltipSide::Above;
      throw StarException(strf("Invalid extension side '{}'", side));
    };
    PaneExtensions::Extension extension;
    if (auto sides = spec.optArray("sides")) {
      if (sides->empty())
        throw StarException("Extension sides must not be empty");
      for (size_t i = 0; i < sides->size(); ++i) {
        auto side = parseSide(sides->at(i).toString());
        if (!side)
          throw GuiException(i == 0 ? "Bottom pane extensions take no sides or height" : "A side pane extension cannot move to the bottom");
        extension.sides.append(*side);
      }
      extension.side = extension.sides.first();
    } else {
      extension.side = parseSide(spec.getString("side", "bottom"));
    }
    extension.size = spec.getInt("size");
    extension.height = spec.optInt("height");
    if (auto images = spec.opt("images"))
      extension.images = {images->getString("header", ""), images->getString("body", ""), images->getString("footer", "")};
    extension.bodyRow = spec.optInt("bodyRow");

    // the children are built before the pane changes, so a failure leaves it
    // as it was
    WidgetPtr container;
    String containerName;
    if (auto children = spec.opt("children")) {
      containerName = spec.getString("name");
      container = make_shared<Widget>();
      GuiReader().construct(*children, container.get());
    }
    RectI region = tooltip->extensions().extendBackground(extension, extension.side ? container : WidgetPtr());
    if (container) {
      container->setPosition(region.min());
      container->setSize(region.size());
      tooltip->addChild(containerName, container);
    }
    return jsonFromRectI(region);
  } else if (name == "addWidget") {
    String widgetName = args.at(1).toString();
    auto widget = GuiReader().makeSingle(widgetName, args.at(0));
    if (!widget)
      throw StarException(strf("Could not create widget '{}'", widgetName));
    tooltip->addChild(widgetName, widget);
    return Json();
  }
  throw StarException(strf("Unknown tooltip operation '{}'", name));
}

LuaCallbacks TooltipHooks::makeTooltipCallbacks(BuildPtr const& build) {
  LuaCallbacks callbacks;

  callbacks.registerCallback("kind", [build]() -> String {
    build->check();
    return tooltipKind(build->tooltip);
  });

  callbacks.registerCallback("itemName", [build]() -> Maybe<String> {
    build->check();
    if (auto item = tooltipItem(build->tooltip))
      return item->name();
    return {};
  });

  callbacks.registerCallback("item", [build]() -> Json {
    build->check();
    if (auto item = tooltipItem(build->tooltip))
      return itemSafeDescriptor(item).toJson();
    return Json();
  });

  callbacks.registerCallback("layout", [build]() -> Json {
    build->check();
    return describeLayout(build->tooltip);
  });

  callbacks.registerCallback("extend", [build](Json spec) -> Json {
    build->check();
    if (spec.contains("children") && !spec.contains("name"))
      spec = spec.set("name", build->nextName());
    JsonArray args{spec};
    Json region = applyTooltipOp(build->tooltip, "extend", args);
    build->ops.append({"tooltip", "extend", std::move(args)});
    return region;
  });

  callbacks.registerCallback("addWidget", [this, build](Json const& config, Maybe<String> const& name) -> LuaCallbacks {
    build->check();
    JsonArray args{config, name.value(build->nextName())};
    applyTooltipOp(build->tooltip, "addWidget", args);
    String widgetName = args[1].toString();
    build->ops.append({"tooltip", "addWidget", std::move(args)});
    auto widget = build->tooltip->fetchChild(widgetName);
    return recordCallbacks(LuaBindings::makeWidgetCallbacks(widget.get(), build->reader), widgetName, build);
  });

  return callbacks;
}

// Wraps widget callbacks so that every call checks the build is still live,
// and every call that changes something is recorded for replay.
LuaCallbacks TooltipHooks::recordCallbacks(LuaCallbacks const& callbacks, String target, BuildPtr const& build) {
  static StringSet const readOnly{"active", "hasFocus", "inMember", "itemGridItems", "itemSlotItem"};
  static StringSet const unreplayable{"bindCanvas", "registerMemberCallback", "playSound"};

  LuaCallbacks result;
  for (auto const& p : callbacks.callbacks()) {
    String name = p.first;
    auto function = p.second;
    bool record = !name.beginsWith("get") && !readOnly.contains(name);
    bool replayable = !unreplayable.contains(name);
    result.registerCallback(name, [build, target, name, function, record, replayable](LuaEngine& engine, LuaVariadic<LuaValue> args) -> LuaVariadic<LuaValue> {
      build->check();
      if (!replayable) {
        build->replayable = false;
      } else if (record && build->replayable) {
        try {
          JsonArray jsonArgs;
          for (auto const& arg : args)
            jsonArgs.append(engine.luaTo<Json>(arg));
          build->ops.append({target, name, std::move(jsonArgs)});
        } catch (LuaConversionException const&) {
          build->replayable = false;
        }
      }
      auto ret = function(engine, args.size(), args.ptr());
      if (auto value = ret.ptr<LuaValue>())
        return {*value};
      return ret.get<LuaVariadic<LuaValue>>();
    });
  }
  return result;
}

}

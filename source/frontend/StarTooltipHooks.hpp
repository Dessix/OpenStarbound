#pragma once

#include "StarLuaComponents.hpp"
#include "StarLruCache.hpp"
#include "StarListener.hpp"

namespace Star {

STAR_CLASS(Pane);
STAR_CLASS(UniverseClient);
STAR_CLASS(Player);
STAR_CLASS(TooltipHooks);

// Runs the scripts listed in /client.config:tooltipScripts. Those scripts
// register named tooltip augmentations, which are called for each newly
// created tooltip. Everything an augmentation does to the tooltip is recorded
// as a list of operations and cached under (augmentation name, item), so an
// unchanged tooltip is rebuilt without calling Lua until the cache for that
// name is cleared.
class TooltipHooks {
public:
  TooltipHooks(UniverseClientPtr client);
  ~TooltipHooks();

  // (Re)starts the configured scripts on first use and after an asset reload.
  // Returns true when the set of augmentations may have changed.
  bool reloadIfNeeded();
  bool enabled() const;

  void update(float dt);

  // Applies every augmentation to a freshly built tooltip. rebuild() re-creates
  // the stock tooltip and is used to recover from a failing augmentation.
  PanePtr apply(PanePtr tooltip, function<PanePtr()> const& rebuild);

  // Clears the cached results of the named augmentation, optionally only those
  // for one item name. Returns false if no such augmentation is registered.
  bool clearCache(String const& name, Maybe<String> const& itemName = {});

  // False while an augmentation is building, whose refresh would rebuild the
  // tooltip again every frame; such a request is logged once per augmentation.
  bool refreshAllowed();

private:
  struct Op {
    String target;
    String name;
    JsonArray args;
  };

  struct CacheEntry {
    String itemName;
    // compared on a hit, as the key holds only their hash
    Json parameters;
    size_t layout;
    List<Op> ops;
  };

  struct Script {
    String path;
    shared_ptr<LuaUpdatableComponent<LuaBaseComponent>> component;
  };

  struct Augmentation {
    String name;
    size_t script;
    Maybe<LuaFunction> build;
    Maybe<StringSet> kinds;
    bool errorLogged = false;
    bool refreshLogged = false;
    HashLruCache<String, CacheEntry> cache;
  };

  struct Build;
  typedef shared_ptr<Build> BuildPtr;

  void stop();
  void logError(Augmentation& augmentation, String const& message);

  bool run(Augmentation& augmentation, PanePtr const& tooltip, List<Op>& ops, bool& replayable);
  bool replay(Augmentation& augmentation, PanePtr const& tooltip, List<Op> const& ops);

  LuaCallbacks makeTooltipCallbacks(BuildPtr const& build);
  LuaCallbacks recordCallbacks(LuaCallbacks const& callbacks, String target, BuildPtr const& build);
  static Json applyTooltipOp(PanePtr const& tooltip, String const& name, JsonArray const& args);

  UniverseClientPtr m_client;
  List<Script> m_scripts;
  List<Augmentation> m_augmentations;
  bool m_dirty;
  Maybe<size_t> m_initializingScript;
  Player* m_player;
  Augmentation* m_building = nullptr;
  CallbackListenerPtr m_reloadListener;
};

}

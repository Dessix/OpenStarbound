# Tooltips

Tooltip augmentations let client-side mods add content to tooltips (item, crafting recipe and simple text tooltips) as they are created. An augmentation is a named Lua function. It is called once per tooltip build, and everything it does to the tooltip is recorded and cached, so later tooltips for the same item are rebuilt by the engine **without calling Lua** until the mod clears the cache.

## Loading a tooltip script

Scripts are listed in `/client.config:tooltipScripts`, which OpenStarbound defines as an empty list. Append to it with a patch guarded by a `test` op:

```json
[
  { "op" : "test", "path" : "/tooltipScripts" },
  { "op" : "add", "path" : "/tooltipScripts/-", "value" : "/scripts/mymod/mytooltip.lua" }
]
```

On builds without this feature (vanilla Starbound, older OpenStarbound) the key is missing, the `test` op fails, and the whole patch file is skipped. Failed `test` ops are logged only at debug level, so the mod is inert and silent there.

Each listed script gets one persistent context, started when the interface loads and restarted on asset reload or when the player changes. The context has the `root`, `sb`, `player`, `script` and `tooltips` tables, the tables shared by all universe client scripts (such as `interface` and `input`), and the usual `init()` / `uninit()`. `update(dt)` is only called if the script enables it with `script.setUpdateDelta(n)`. It is disabled by default, so an idle tooltip script costs nothing per frame.

These scripts are not `universeScriptContexts` entries because those start once per connection, update every tick and have no `player` table. A tooltip script's registrations depend on the assets and the player, so it is restarted whenever either changes, and it stays idle until it asks for updates.

When no script registers an augmentation, the engine does no Lua work for tooltips at all.

---

#### `void` tooltips.register(`String` name, `function` build, [`Json` options])

Registers an augmentation. This may only be called while the script's `init()` runs. `name` identifies the augmentation in its cache, in error messages and to `interface.clearTooltipCache`. Registering a name that is already registered replaces it.

`build(tooltip, widget)` is called for each tooltip that is not cached; see below.

Options:
* `kinds`: a list of tooltip kinds to be called for (`"item"`, `"recipe"`, `"text"`, `"other"`). By default the augmentation is called for all of them.

Augmentations run in the order their scripts are listed and then in registration order; re-registering a name moves it to the end.

## Caching

A result is cached under the augmentation name and the tooltip's item: tooltip kind, item name, count and parameters. It is replayed only onto a tooltip whose stock layout (background images, size, and every widget's name, position, size and label text) matches the one it was built against. Otherwise the augmentation runs again. Each augmentation keeps its 128 most recently used results. Asset reload clears everything.

**The engine never invalidates a result on its own because game state changed.** That includes inventory contents, learned blueprints, techs and quest progress. If your content depends on anything other than the item, call `interface.clearTooltipCache(name, [itemName])` when that thing changes, and `interface.refreshTooltip()` to rebuild a tooltip that is showing. Both are available to tooltip scripts and to every other universe client script. For example, to show a detailed view while a key is held, read `input.bindHeld(category, bind)` in `build`, and from `update` call both when that value changes.

What is recorded:
* `tooltip.extend` and `tooltip.addWidget`
* every `widget.*` call that changes something (setters, `addChild`, `removeChild`, `addListItem`, ...). Getters are not recorded.

A result that uses `widget.bindCanvas`, `widget.registerMemberCallback` or `widget.playSound`, or passes a widget call an argument that cannot be stored as JSON (such as a function), is not cached, so the augmentation runs again on every build.

## The `tooltip` table

`tooltip` and `widget` are valid only during the `build` call. Using them afterwards raises a Lua error.

#### `String` tooltip.kind()

`"item"` (inventory, hotbar, container and merchant item tooltips), `"recipe"` (crafting window entries), `"text"` (simple text tooltips) or `"other"`.

#### `String` tooltip.itemName()

The item name for `item` and `recipe` tooltips (the recipe output), otherwise nil.

#### `ItemDescriptor` tooltip.item()

The item descriptor, including parameters, or nil.

#### `Json` tooltip.layout()

Describes the tooltip as it is now, in pane coordinates (origin at the bottom left):

```lua
{
  size = {w, h},
  background = { header = {image = "...", size = {w, h}}, body = {...}, footer = {...} }, -- entries may be nil
  extensions = { bottom = 0, left = 0, right = 0, below = 0, above = 0 },  -- space already added by augmentations (bottom: rows; others: summed panel widths)
  widgets = {
    { name = "descriptionLabel", type = "label", position = {x, y}, size = {w, h}, visible = true, text = "...",
      children = { ... } },
    ...
  }
}
```

`type` is one of `label`, `image`, `imageStretch`, `list`, `itemslot`, `canvas`, `button` or `widget`.

#### `RectI` tooltip.extend(`Json` spec)

Grows the tooltip and returns the new region `{x1, y1, x2, y2}` in pane coordinates. The new region is drawn with the tooltip's own art, so it matches whatever interface skin is installed:

* `side = "bottom"` (default): inserts `size` pixels directly above the footer. Everything above the footer, including the title and icon, moves up. Footer content such as price and rarity stays in place. The region is filled with one row of the body image, stretched.
* `side = "right"` / `"left"`: adds a panel `size` pixels wide beside the stock panel, as tall as the stock panel (including bottom extensions) or `height` pixels if given. Several panels on one side stack outwards in the order added.
* `side = "below"` / `"above"`: adds a panel `size` pixels wide and `height` pixels tall (default: the stock panel's height) under or over the stock panel. It shares the stock panel's left edge, or its right edge when that keeps it on screen and clear of left/right panels. Several below (above) panels stack outwards in the order added.
* `sides = {"right", "left"}`: instead of `side`, the sides a panel may go on, in order of preference. Any of `"left"`, `"right"`, `"below"` and `"above"` may be listed (not `"bottom"`). The returned region is for the first side.

A panel is drawn as a complete panel from the tooltip's header, body and footer art, resized at a plain column found from the image itself, so restyled tooltip art works too. `size` is the visible panel; the art's shadow and transparent margins come on top. Visible edges are `/panes.config:tooltipPanelGap` interface pixels apart (default 1).

Placement: panels never overlap each other or the stock panel, and panels from different augmentations are treated alike. The engine picks, for each panel, the first side in its list that fits on screen, keeping the sides chosen when the tooltip was first shown while they still fit. Left/right panels are placed first and are never moved by below/above panels. Only if no combination of sides fits does the stock panel move away from its usual spot, by the least distance that makes one fit. **A tooltip without panels is placed exactly as by the stock game.**

Clipping and scrolling: a panel taller than the space the window leaves it is clipped, and its content scrolls on its own: it holds at the top for `/panes.config:tooltipScrollHold` seconds (default 1.5), scrolls down at `/panes.config:tooltipScrollSpeed` interface pixels per second (default 25; 0 turns scrolling off), holds at the bottom, and starts over. A dim `…` marks an edge with hidden content. Each new display, including a refresh, starts at the top. Scrolling and side choice are not cached and do not call the augmentation again. Content added with `children` moves with its panel; anything else stays with the stock panel.

Optional fields:
* `images = {header =, body =, footer =}` overrides the art for this region.
* `height`: panels only, their height in pixels.
* `bodyRow`: which row of the body image to stretch, counting from the bottom. Bottom strips default to the middle row; panels default to the row nearest the middle with the least inner decoration (so a description box or icon frame in the body art is not stretched along the panel).
* `children`: widget configs, as in a `.config`/`.tooltip` file, built into a container that covers the region, so positions are relative to the region.
* `name`: the container's name. Defaults to `<augmentation name>.<n>`. Address its children as `"<name>.<child>"` in `widget` calls.

Panels without `height` follow the stock panel's height, including bottom extensions added after them. Tooltips without a background (for example simple text tooltips, which use an `imageStretch` widget) grow, but nothing is drawn behind the new region.

#### `WidgetCallbacks` tooltip.addWidget(`Json` config, [`String` name])

Adds one widget at pane coordinates, like `pane.addWidget`. Returns `widget`-style callbacks for the new widget.

## The `widget` table

The standard [widget](widget.md) callbacks, bound to the tooltip pane, so an augmentation can also change existing content (`widget.setText("descriptionLabel", ...)`).

## Errors

If `build` raises an error, or replaying a cached result fails, the stock tooltip is rebuilt and the other augmentations' results are applied again, without this augmentation's content. If an earlier result is not cacheable (see above) it cannot be applied again, and the stock tooltip is shown. The first error of each augmentation is logged with its name and script path. Later errors are not logged.

## Example

```lua
function init()
  tooltips.register("myMod.value", function(tooltip, widget)
    local value = root.itemConfig(tooltip.itemName()).config.price or 0
    local region = tooltip.extend{ side = "bottom", size = 12 }
    tooltip.addWidget({ type = "label", position = {region[1] + 10, region[2] + 2}, value = "Base value: " .. value })
  end, { kinds = {"item"} })
end
```

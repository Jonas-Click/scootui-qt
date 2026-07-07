# Themes & Layout Packs

ScootUI supports two levels of customization, both without recompiling:

1. **Color themes** — a JSON file that recolors the whole UI.
2. **Layout packs** — a directory with QML that completely replaces the
   cluster screen. Full creative freedom.

Both live under the scooter's `/data` partition, survive OTA updates, and
hot-reload while the dashboard is running: `scp` a file, watch the UI update.

Three settings, three separate concepts:

| Setting | Meaning | Files | Redis key |
|---|---|---|---|
| **Appearance** | `auto` / `dark` / `light` — picks the variant of the active color theme (auto follows the ambient light sensor) | — | `dashboard.theme` |
| **Color Theme** | which palette recolors the UI; every theme carries a dark **and** a light variant | `/data/scootui/themes/<name>.json` | `dashboard.color-theme` |
| **Layout** | which cluster design is shown | `/data/scootui/layouts/<name>/` | `dashboard.layout` |

The three combine freely: any layout renders in any color theme, and any
color theme renders in dark and light appearance.

On the desktop simulator, set `SCOOTUI_DATA_DIR=/path/to/dir` and use
`<dir>/scootui/themes/`, `<dir>/scootui/layouts/`.

Setting via redis (from an SSH session on the scooter):

```sh
redis-cli HSET settings dashboard.color-theme sunset
redis-cli HSET settings dashboard.layout minimal
```

A broken theme or layout never bricks the dashboard: the built-in default
takes over, a toast explains what failed, and the menu stays reachable.
Fix the file and it hot-reloads back in.

## Color themes

A theme overrides any subset of the design tokens, for the **dark** and
**light** variant separately. The Appearance setting picks which variant
of your theme is shown. Missing tokens inherit the default palette — per
variant, never across — so every theme automatically works in both dark
and light mode, and this is a complete, valid theme:

```json
{
    "name": "Red Accent",
    "schemaVersion": 1,
    "dark":  { "accent": "#E53935" },
    "light": { "accent": "#C62828" }
}
```

Colors are `#RRGGBB` or `#AARRGGBB` (alpha first). Invalid values fall back
to the default for that token (with a warning in the journal). A user theme
with the same name as a built-in (`default`, `amber`) shadows it.

See `examples/themes/` for a starter set and `assets/themes/default.json`
for the full reference palette.

### Token reference

| Token | Used for | Default dark | Default light |
|---|---|---|---|
| `text` | Primary text | `#FFFFFF` | `#000000` |
| `textSecondary` | Secondary text (units, labels) | `#99FFFFFF` | `#8A000000` |
| `textTertiary` | De-emphasized text | `#4DFFFFFF` | `#1F000000` |
| `textHint` | Hints, placeholders | `#8AFFFFFF` | `#61000000` |
| `background` | Screen background | `#000000` | `#FFFFFF` |
| `surface` | Cards, panels | `#1E1E1E` | `#F5F5F5` |
| `border` | Hairline borders | `#1AFFFFFF` | `#1F000000` |
| `arcBackground` | Speedometer arc track | `#424242` | `#E0E0E0` |
| `powerBarBg` | Power bar track | `#424242` | `#E0E0E0` |
| `powerZeroMark` | Power bar zero marker | `#66FFFFFF` | `#61000000` |
| `accent` | Speed fill, highlights, links | `#2196F3` | `#2196F3` |
| `speedFillHigh` | Speed fill approaching max | `#9C27B0` | `#9C27B0` |
| `overspeedA` / `overspeedB` | Overspeed pulse endpoints | `#9C27B0` / `#E91E63` | same |
| `speedRegen` | Arc tint while regenerating | `#4DFF0000` | `#4DFF0000` |
| `speedError` | Arc glow on ECU comm loss | `#F44336` | `#F44336` |
| `speedTick` | Arc ticks + minor labels | `#80FFFFFF` | `#1F000000` |
| `speedLabelMajor` | Major speed labels | `#CCFFFFFF` | `#4D000000` |
| `statusSuccess` | Success chips/toasts | `#2E7D32` | `#2E7D32` |
| `statusWarning` | Warning chips/toasts | `#E65100` | `#E65100` |
| `statusError` | Error chips/toasts | `#C62828` | `#C62828` |
| `statusNeutral` | Neutral chips | `#424242` | `#424242` |
| `statusInfo` | Info chips/toasts | `#1565C0` | `#1565C0` |

In QML the tokens are exposed on `themeStore` (e.g. `themeStore.accent`;
`text`/`background`/`surface`/`border` become `textColor`,
`backgroundColor`, `surfaceColor`, `borderColor`). Layout packs can also
look tokens up by name: `themeStore.token("accent")`.

### Per-theme map styles

A theme may ship its own MapLibre styles, paths relative to the theme file:

```json
{ "map": { "dark": "styles/mydark.json", "light": "styles/mylight.json" } }
```

Give the two files distinct names. Omit `map` to keep the built-in styles.

## Layout packs

A layout pack replaces the cluster screen (the main riding screen). It is a
directory containing a manifest and a QML entry point:

```
/data/scootui/layouts/minimal/
├── layout.json
├── Cluster.qml
└── ... (any assets/QML your design needs, referenced by relative URL)
```

`layout.json`:

```json
{
    "name": "Minimal",
    "author": "you",
    "schemaVersion": 1,
    "apiVersion": 1,
    "cluster": "Cluster.qml"
}
```

`apiVersion` is checked at load; packs written against a newer API are
rejected with a clear message instead of half-working. `cluster` names the
entry-point QML file (default `Cluster.qml`).

Start from an example — copy it, rename it, make it yours:

- `examples/layouts/minimal/` — the smallest useful pack: one QML file,
  giant speed readout, reused built-in blinker icons.
- `examples/layouts/cards/` — speed plus info cards, demonstrating
  pack-local icons and the battery/range/trip store APIs.

### What your QML can use (designer API)

All stores are global context properties; the ones below are the **stable
designer API**. Anything not listed here may change between releases.

- `engineStore` — `speed` (km/h), `rpm`, `odometer` (meters), `motorCurrent`
  (A, negative = regen), `throttle`, `faultCode` (20 = ECU comm loss),
  `rawSpeed`, `hasRawSpeed`
- `vehicleStore` — `state` (compare with `Scooter.VehicleState.*` after
  `import ScootUI 1.0`), `blinkerState` (0 off, 1 left, 2 right, 3 both),
  `blinkOpacity` (shared blink clock, animate your indicators with it),
  `brakeLeft`, `brakeRight`, `kickstand`, `isUnableToDrive`
- `battery0Store` / `battery1Store` — `present`, `charge` (0-100),
  `voltage`, `batteryState`
- `tripStore` — `distance` (m), `duration` (s), `averageSpeed`
- `gpsStore`, `bluetoothStore`, `internetStore` — connectivity status
- `themeStore` — all tokens above, plus `isDark`, the `font*` type scale
  (`fontDisplay` 96 … `fontMicro` 10) and `radius*` constants
- `settingsStore` — read-only user settings; `translations` — UI strings
- `appWidth`, `appHeight`, `scaleFactor` — display geometry

Built-in widgets and icons can be reused from the qrc bundle:

```qml
import "qrc:/ScootUI/qml/widgets/components"   // SvgIcon, MaterialIcon, ...

SvgIcon { source: "qrc:/ScootUI/assets/icons/librescoot-turn-left.svg"; color: themeStore.accent }
```

If your root item exposes `readonly property real bottomBarHeight`, the
blinker overlay positions itself above your bottom bar (default 48).

### Custom icons

Ship icons inside the pack and reference them relative to your QML file —
no registration, no rebuild. For monochrome icons, draw one white SVG and
render it through the built-in `SvgIcon`, which tints at runtime with
whatever theme token you bind — one file works in every color theme, dark
and light:

```qml
import "qrc:/ScootUI/qml/widgets/components"

SvgIcon {
    source: Qt.resolvedUrl("icons/bolt.svg")   // resolved relative to this file
    color: charge > 20 ? themeStore.accent : themeStore.statusError
}
```

Full-color artwork works too: use a plain `Image` (or `SvgIcon` with
`tintEnabled: false`) with any format Qt Quick supports (SVG, PNG, ...).
Don't rely on emoji for iconography — the display font on the scooter
ships a reduced glyph set; use an SVG instead.
`examples/layouts/cards/icons/` shows the pattern.

### Rules of the road

- **Display**: 480×480. The physical bezel is round — keep critical info
  out of the extreme corners.
- **Performance**: this runs on an i.MX6. Prefer anchors/Layouts and
  `Loader { asynchronous: true }` for heavy parts; avoid re-painting huge
  `Canvas` items every frame. The first load of a new pack compiles the
  QML once (cached afterwards in `/data/scootui/.qmlcache`).
- **Overlays stay**: menu, toasts, blinker overlay, shutdown and lock
  screens render on top of your layout. Your pack replaces the cluster
  screen only — which is also why a broken pack can't lock you out.
- **Trust**: a layout pack is QML/JavaScript, i.e. arbitrary code running
  in the dashboard. Install only packs you trust — the same rule as for
  anything else you put on a rooted scooter. ScootUI never downloads packs
  on its own.

### Developer workflow

1. Fastest loop: desktop simulator with `SCOOTUI_DATA_DIR` pointing at your
   working directory — full hot reload, no hardware needed. Use
   `SCOOTUI_COLOR_THEME=<name>` / `SCOOTUI_LAYOUT=<name>` to start straight
   into your design without touching the menu.
2. On the scooter: `scp -r mylayout/ root@scooter:/data/scootui/layouts/`;
   edits hot-reload live. Journal (`journalctl -u scootui -f`) shows load
   errors with file/line.

# Caps Word AZ (`caps_word_az`)

A custom ZMK behavior implementing [caps word] with a configurable
**shift-list**, backported for the ZMK fork this config pins
(`cormoran/zmk` `v0.3-branch+dya`).

[caps word]: https://zmk.dev/docs/keymaps/behaviors/caps-word

## Why it exists

The pinned ZMK fork predates the caps-word rework: its stock `&caps_word`
only shifts real A–Z HID usages and offers no `shift-list` /
`continue-list` / `no-default-keys` properties. That breaks non-QWERTY
layouts, where letters are sent from other physical positions — e.g. on an
emulated AZERTY layout, `U` is sent from the `SEMI` position, so stock caps
word leaves it lowercase.

`caps_word_az` restores the newer semantics locally:

- `shift-list` — keys that get the configured mods (right shift) applied
  while caps word is active;
- `continue-list` — keys that keep caps word alive without shifting;
- `no-default-keys` — disable the implicit A–Z / 0–9 defaults so only the
  lists above apply.

Each keyboard-layout alias header defines its own lists, e.g. in
`include/aekeynox/aliases/qwerty.h`:

```c
#define CAPS_WORD_SHIFT_LIST \
    Q  W  E  R  T    Y  U  I  O  P \
    A  S  D  F  G    H  J  K  L \
    Z  X  C  V  B    N  M
```

The behavior is instantiated in `include/aekeynox/mappings.dtsi` and
referenced by the `unshift_caps` macro in `include/aekeynox/hold_taps.dtsi`
(double-tap Shift = caps word, after cancelling sticky shift).

## Files

| File | Purpose |
| ---- | ------- |
| `src/behavior_caps_word_az.c` | The behavior itself (listener + driver API) |
| `dts/bindings/behavior/zmk,behavior-caps-word-az.yaml` | Devicetree binding |
| `Kconfig` | `CONFIG_ZMK_BEHAVIOR_CAPS_WORD_AZ` (default `y`) |
| `CMakeLists.txt` | Compiles the behavior into the app target (split central only) |
| `zephyr/module.yml` | Registers the module's CMake/Kconfig |
| `include/aekeynox/mappings.dtsi` | Instantiates the `caps_word_az` node |

Because the behavior subscribes to `zmk_keycode_state_changed`, which is
generated centrally on split keyboards, it is only built for the central
half — same approach as `zmk-behavior-dead-key`.

## History of a nasty bug

The first version of the behavior (commit `8399123`) compiled but never
worked: **no key was ever shifted, and caps word deactivated on the very
first keypress** — for every layout, QWERTY included. This looked like a
mysterious split-communication issue and was chased for several commits
with USB logging, then underglow color signals, before the real cause was
found in the property-parsing macros.

The bug: `LISTIFY`'s first macro parameter is the **loop index**
(0, 1, 2…), not the property value. The original macros

```c
#define SHIFT_ITEM(i, n) {.page = ZMK_HID_USAGE_PAGE(i), .id = ZMK_HID_USAGE_ID(i)}
```

built the shift-list out of nonsense entries (`{page 0, id 0}`,
`{page 0, id 1}`, …) instead of the actual key usages from the devicetree
property. Consequences:

- nothing ever matched the shift-list → no shift applied;
- with `no-default-keys`, unmatched keys deactivate caps word → it died
  instantly on the first QWERTY letter (whose continue-list is empty).

The fix (commit `255a63b`) fetches the real property values, exactly like
upstream ZMK's `behavior_caps_word.c` does for `continue_list`:

```c
#define SHIFT_ITEM(i, n) \
  {.page  = ZMK_HID_USAGE_PAGE(DT_INST_PROP_BY_IDX(n, shift_list, i)), \
   .id    = ZMK_HID_USAGE_ID(DT_INST_PROP_BY_IDX(n, shift_list, i))}
```

With this in place, caps word works across layouts — including the AZERTY
case that motivated it (`U` on `SEMI` gets shifted correctly) and plain
QWERTY.

# Effect presets and editing workflow

Each effect has a compact **Presets** dropdown in its header in Pedalboard,
Rack and Single effect views. Open that effect's menu directly; selecting a
different pedal in the signal chain is unnecessary.

Choose a saved sound to apply it to that effect, or choose **Save effect
preset…** to name the current settings. An existing name requires an explicit
overwrite confirmation. **Manage presets…** keeps the existing rename, copy,
reorder, delete, import and export tools. Import is also available when the
library is empty.

The library belongs to the effect type, so the same sounds are available for
that effect in other pedalboards. Loading copies the settings; saving a sound
does not automatically change other pedalboards. Save the whole pedalboard
after loading an effect preset if you want to retain that combination.

The button deliberately says **Presets**, not the last loaded sound's name:
the host does not store an effect-preset association or track whether current
settings still match it. A name that remained after knob edits, hardware
changes or a board load would imply a relationship that is not there.

Copy and paste are in the same menu, with **Paste effect (replace)** making its
scope explicit. These structural operations hide when editing is locked;
loading and saving effect settings remain available. Empty slots and splits
retain their toolbar clipboard menu.

Pedalboard cards now have an explicit collapse/expand button for keyboard and
touch access; double-clicking their headers still works. Supr knobs respond to
the wheel only after being focused by a click or keyboard navigation, so
scrolling across an untouched knob does not change the sound.

After loading an effect preset, a compact bar provides **Undo** and **Redo**
for that most recent load. It restores the effect's sound without replacing
other pedals or its routing and bypass settings. Further sound edits, changing
boards, or reconnecting clear this comparison so undo cannot overwrite newer
work. This is effect-preset audition history, not a general undo of every edit.

Keyboard shortcuts work outside text fields, menus and dialogs:

| Action | macOS | Windows/Linux |
| --- | --- | --- |
| Undo effect preset | Cmd+Z | Ctrl+Z |
| Redo effect preset | Cmd+Shift+Z | Ctrl+Shift+Z or Ctrl+Y |
| Save current pedalboard | Cmd+S | Ctrl+S |

Focused knobs retain their arrow-key adjustments and Shift fine adjustment.

## Recommended next improvements

1. **Exact value entry.** Make a knob's existing value readout editable on
   activation. Preserve the quiet appearance at rest and reuse LV2 ranges,
   units and snapping. This would make matching gain, EQ and time values
   between sounds considerably easier.
2. **Visible preset identity, then selective propagation.** Tracking a saved
   sound and its modified state needs persistent host metadata and comparison
   of complete plugin state. Once reliable, a separate, explicit action could
   apply a revised effect sound to selected board presets. Avoid silently
   changing an entire setlist when one library entry is saved.

## Validation

Run `node vite/test/effect-presets.mjs`, `node vite/test/knob-drag.mjs`,
`node vite/test/keyboard-shortcuts.mjs`,
`node vite/test/effect-preset-undo.mjs`,
`npm run lint --prefix vite` and `npm run build --prefix vite`.

The selector check exercises the production component with model doubles:
initial load, correct effect targeting, clipboard sharing, editing lock,
overwrite/cancel, write failures, duplicate submissions, changing boards
during a save, reconnect and listener cleanup. It never alters live presets.
Browser checks should cover narrow and desktop layouts, menu context while a
different effect is selected, save cancellation, management of an empty
library, collapse/expand, and the single-effect and stacked views.

`test/effect_preset_undo_integration.mjs` verifies the real server's atomic
capture/load, undo, redo, conflict rejection and preservation of other effects
and metadata. It refuses hosts with real audio or enabled GPIO. Run it only
against a separate test data root and dummy audio device; the optional board
fixture and created presets belong in that test instance.

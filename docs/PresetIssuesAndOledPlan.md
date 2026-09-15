# Preset and OLED implementation notes

This document records the durable behavior and validation contract for the
preset, hardware-display, and OLED artwork changes. See
[GPIO hardware controls](GpioControls.md) for user instructions and
[Supr UI and hardware design](SUPRDESIGN.md) for the shared implementation
guide.

## Preset behavior

### Stable alphabetical lists

Published current-bank and import-bank lists use the server's existing
case- and accent-insensitive collation. Browser selection, preset management,
next/previous navigation, and hardware browsing all use that order.

The stored bank order is deliberately unchanged so existing MIDI program
number assignments remain stable. Drag reordering is therefore disabled in
the preset manager.

### Reliable Save As

After a successful write, the server commits the new selection, live name,
and clean state. The browser waits for that reply without performing a second
reload. Saving into another bank preserves the active preset and its dirty
state, and a stale insertion anchor safely falls back to appending.

Save As detects an exact existing name and asks for confirmation before
overwriting it. The confirmation includes the target preset ID, which the
server validates against the current name. Missing or renamed targets and new
name collisions fail without loading another preset. An overwrite retains the
target ID; cancelling retains the entered name, and write errors leave the
form open for retry.

Open, write, and close failures preserve both the in-memory state and the
on-disk rollback data. Upload handlers also close and check their temporary
files before exposing them to the importer, including when a small ZIP's
headers and body arrive in one read.

## OLED preset notices

Every successful preset or bank load posts a two-second notice. Browser, MIDI,
mapped switch, next/previous, and ANO Select loads all converge on these paths.
Browsing, editing, Save As, rename, and failed loads do not post notices.
Identity includes both bank and preset ID, and a rapid load replaces the
existing notice.

The notice uses only x=0..127 and y=24..63. The tuner's strobe and pitch needle
in y=0..23 remain live. Artwork occupies the complete 128×40 lower strip and
replaces the preset name. Without artwork, a one- or two-line name is centred
in the strip. Long names are ellipsized, and unsupported Unicode becomes one
question mark per code point. Name and artwork notices can be enabled
independently.

The notice has display priority for its full two seconds. Controls continue to
respond, but parameter, action, and navigation overlays wait. Only the latest
pending overlay appears after the notice expires, with its complete configured
timeout. A new preset load clears stale pending feedback. Initial mapped-input
refreshes remain silent, while a real hardware change still produces feedback.
ANO Select clears preset browsing and returns to Parameters after loading.
Blank mode remains blank.

Mapped preset and bank actions share a 100 ms cooldown after a successful load
to suppress duplicate encoder-button events. Other mapped actions and
browser/MIDI loads are unaffected.

## Artwork editor and persistence

Choose **OLED artwork…** from the preset menu. The editor accepts a local PNG,
JPEG, or WebP image, provides fit and crop modes, threshold and invert controls,
and shows an enlarged preview in the tuner layout. Input is limited to 2 MiB
and 2048×2048 decoded pixels before canvas processing begins.

Choose **Apply**, then save the preset. Removing artwork also requires Apply
and a preset save. `Pedalboard.oledArtwork` stores exactly 640 integer bytes:
128×40 pixels, row-major, most-significant bit first, with 1 meaning lit. The
server validates the length and every value before narrowing to a byte. Missing
metadata remains valid for legacy presets, and development-era 384-byte artwork
is expanded to the new height when read.

Applying artwork checks the current bank and preset identity and marks only
the metadata dirty; it does not rebuild the audio graph. Copies, Save As,
overwrite, and preset/bank export and import preserve the bitmap. The device
therefore needs no runtime image files or image decoder.

## Boot screen

The compiled 128×64 SuprDuprNatural bitmap is white on black and uses a simple
left-to-right clip that remains legible at the display's native resolution.
The reveal takes 800 ms and holds until 2.2 seconds after startup, then yields
to the passive display.

The boot screen appears once per service start. Hardware activity cancels it,
Blank mode suppresses it, and browser, settings, or display reconnects do not
replay it. Startup does not generate a loaded-preset notice.

![OLED boot-wipe renderer frames](img/oled/boot-wipe-frames.png)

The source raster and generated bitmap are stored under `docs/img/oled/`. The
preview above is generated from the production drawing code rather than from a
separate mock-up.

## Timing and hardware constraints

Only the existing display worker renders and writes to OLED I²C. Model
callbacks copy mutex-protected snapshots; audio callbacks continue to feed the
existing lock-free sample rings. Tuner analysis remains active whenever Tuner
is the selected passive screen, including while notices, the boot screen, or
overlays are visible.

Each frame draws the base screen, composites the active notice, and flushes
once. Existing chunked I²C writes and yields are preserved. Reveals use elapsed
monotonic time, skip missed frames, and respect the configured refresh interval
(200 ms by default, 50 ms minimum). They add no timer thread, frame queue, or
automatic refresh-rate increase. Missing or failed OLED hardware is nonfatal
and retries after five seconds.

## Automated validation

Run the portable regression checks from the repository root:

```sh
python3 test/preset_save_regression.py
python3 test/gpio_preset_cooldown.py
python3 test/oled_regression.py
node vite/test/preset-save.mjs
node vite/test/oled-artwork.mjs
npm run lint --prefix vite
npm run build --prefix vite
```

The production-code checks cover save, collision, and rollback paths; artwork
parsing, legacy migration, copies, full-height rendering, and name suppression;
Unicode and clipping; the protected tuner region; one flush per frame; notice
replacement and expiry; independent notice settings; Blank mode; overlay
priority; boot timing; tuner sample consumption; display failure; and
hardware-load cooldown.

The websocket integration test (`node test/preset_oled_integration.mjs`) must
run against an isolated host and data root, never against live user presets.
Its `--verify-restart` mode verifies persisted artwork, and its
`--failed-write` mode verifies that a permission-denied save preserves the live
board, selection, name, and dirty state.

On a Raspberry Pi build, also run the native GPIO test filter:

```sh
build/src/pipedaltest "[gpio]"
```

## Hardware acceptance checklist

Software tests cannot replace a short physical check:

- Confirm that the boot wipe and text-only preset names are readable.
- Load presets from the browser, MIDI, mapped buttons, ANO Select, and
  next/previous bank controls, including rapid switching.
- Check Parameters, Waveform, Tuner, and Blank modes and overlay expiry.
- Confirm that artwork fills the lower 128×40 region, replaces the preset name,
  and leaves the tuner strobe and needle live above it.
- Play low, flat, sharp, and in-tune notes while exercising the encoders.
- Confirm that OLED reconnect recovery works when the display can be safely
  disconnected.

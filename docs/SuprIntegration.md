# Supr collection integration

This fork supplies the custom faces and transport-delay compensation for the
companion [SuprPedals](https://github.com/SuprDuprNatural/SuprPedals) collection.
DSP/LV2 contracts and current deployment acceptance live in that repository's
`docs/RELIABILITY.md` and `docs/RELEASE_STATUS.md`. Keep the repositories together
when deploying; do not push this fork to upstream `origin`. Its publication
remote is `fork` (SuprDuprNatural/pipedal).

## Host paths

`LatencyCompensation.hpp` implements bounded delay, cumulative serial latency
and nested split merges. `Lv2Pedalboard::PrepareItems` allocates merge history
before audio starts, propagates reported latencies in processing order and
resets delay state on activation. `Lv2Effect` reads the LV2 latency designation,
includes fixed-block staging and preserves transport delay in host soft bypass.

Each delay is bounded to 65,536 samples/channel. Changes crossfade over 64
samples; limit flags propagate rather than claiming exact compensation past
the bound. History is preallocated. Latency values can be nonfinite, so
`Lv2Effect.cpp` is compiled without fast-math. Filters/cabinets/model phase and
internal plugin bypass remain separate from transport-delay compensation.
Echo/Space tails require Send off with the effect still enabled.

Build and run `latencyCompensationTest` for impulse, serial/nested routing,
bypass delay, dynamic changes, irregular/in-place blocks, bounds and reset.
Also check actual host bypass/staging and native deadlines under load; the
standalone helper test does not exercise the entire audio host.

## Faces

`ControlViewFactory.tsx` registers exact Supr URIs. `SuprPedalViews.tsx` lays out
the collection using shared controls; `SuprNamView.tsx` renders model/routing
information. `SuprLearnActions.tsx` applies DSP recommendations through ordinary
saved controls and resubscribes on Ready. `SuprEchoActions.tsx` uses joined
quarter/eighth/dotted-eighth buttons and quarter-note Tap. Echo/Space omit duck
meters and put Send in Return; Hold remains an LV2/MIDI port without a face
button. Shape and Phase are included. OctavePlus expansion is deferred.

Follow `SUPRDESIGN.md`: values and meters come from the host/plugin, controls
must follow presets and reconnect, and DOM measurements establish geometry.
At 375 px the current Echo/Space faces fit 292 px; the Compressor uses wrapping
columns so its meter, timing knobs and Held Peak selector remain reachable.
The CMake UI dependencies include all `Supr*.tsx` files.

## Deployment

Build the UI with `npm run build` in `vite`. `restage-supr-ui.sh` stages the
**complete** dist, including shared chunks, CSS, fonts and static assets. It
never installs or restarts audio. Optional `RSYNC_RSH` passes verified SSH
settings through to rsync. Use the companion repository's deployment notes
for the current Pi source/build locations, backup and installed-file evidence.

Before restart, save the latest *live* `currentPedalboard` over websocket;
saved presets omit unsaved edits. Back up the host, bundles, UI, service and
configuration. Install fresh matching host/plugins/TTL and entire dist, restore
the board, verify values and hard-refresh the browser. Preserve rollback
archives outside Git; never publish models, private board JSON or recordings.

# GPIO encoder follow-up

Last updated: 2026-08-02

## Current status

The OLED navigation, four parameter encoders, and LED matrix work well overall,
and the random navigation changes that occurred while the controls were
untouched have been fixed.

The navigation encoder's remaining unresponsiveness has now been traced to
software rather than to I2C bandwidth or contacts. Two causes were found, both
introduced by the previous session's fixes for the phantom-navigation problem.
Both are addressed; what remains is physical verification on the pedal.

Do not trade the current idle stability for apparent responsiveness: an
untouched control must never navigate or change a value. Everything below was
designed to keep that guarantee.

## What was wrong

### The idle gate discarded whole gestures

`SeesawNavigationEncoder::ReadEncoderPosition` required movement to arrive from
rest in steps of two counts or fewer, confirmed across two samples, before any
movement was released. While a sample was withheld the trusted baseline did not
advance, so the accumulated difference grew, quickly passed the eight-count
active ceiling, and was then rejected for the rest of the gesture. When the
player stopped turning, the whole accumulated movement was discarded.

The behaviour was bimodal rather than merely laggy: a gesture that began gently
worked, and a gesture that began briskly produced nothing at all. Driving the
extracted state machine over synthetic sample sequences:

| gesture | physical | applied (old) |
| --- | --- | --- |
| one detent, then stop | +1 | +1 |
| slow turn, 1 count per sample | +6 | +6 |
| one detent bouncing to 3 counts | +3 | **0** |
| brisk flick, 3 counts per sample | +12 | **0** |
| fast spin, 6 counts per sample | +24 | **0** |

This also explains the anticlockwise asymmetry. A marginal phase contact that
occasionally bounces a detent to three or more counts does not merely add noise
under this rule -- it makes that detent vanish completely. The physical checks
below are still worth doing, but the software was converting a small electrical
imperfection into total loss.

### The stale-read fix collapsed the sample rate

Commit `b1d211b` gave every ANO register read the 8 ms response delay used by
Adafruit's Linux driver. Because all five devices were polled sequentially on a
single worker, with those delays taken as blocking sleeps, the loop budget was:

| | |
| --- | --- |
| 4 parameter encoders x 3 reads x 300 us | 3.6 ms |
| ANO x 2 reads x 8000 us | 16.0 ms |
| loop tail sleep | 1.0 ms |
| ~14 register reads of wire time at 400 kHz | ~2.8 ms |
| **total** | **~23 ms, about 43 Hz** |

Roughly 70% of the input thread's life was spent sleeping inside ANO reads, and
that sleep also blocked the four parameter encoders. Before `b1d211b` the same
loop ran at about 8.5 ms, so the stale-read fix cost roughly 2.7x in sample
rate for every input on the pedal.

The configured `encoderPollIntervalMs_ = 1` was inert: the loop could not come
close to a 1 ms cadence, so the gate had always expired.

## What changed

### Confirm and carry, instead of a step-size gate

The position filter is now `GpioNavigationPositionFilter` in `src/Gpio.hpp`,
deliberately free of I2C so it can be tested directly. From rest it still holds
one sample back and releases it only once a second sample agrees -- either the
position stayed put, or it carried on in the same direction -- so a one-shot
corrupt word is never confirmed. The differences are that confirmation releases
the *whole* accumulated movement, and that the plausibility ceiling is measured
against the time since the last *trusted* position rather than the last sample.

That second point matters more than it looks. Measuring the ceiling from the
previous sample, while the baseline is deliberately held still awaiting
confirmation, lets a fast turn outrun its own ceiling and be rejected -- which
is exactly how the original gate discarded brisk gestures. The unit tests catch
this: an early version of the fix failed the fast-spin cases for precisely that
reason.

The ceiling scales at two counts per millisecond since the trusted baseline,
with a floor of 8 and a cap of 64 counts. The floor covers the normal fast
cadence, and the cap bounds how far a single confirmation can move a menu.

### Two input workers

The ANO now polls on its own thread. Its settling delays are an order of
magnitude longer than the parameter encoders', so a shared worker made each
device wait on the other. Both workers dispatch through the model's serialized
post queue (`DBusDispatcher::Post`, mutex-guarded with an atomic handle
counter), so the split adds no locking to event handling. The refresh flag was
split per worker; a single shared `exchange` would have let whichever thread ran
first swallow the other's refresh.

### Detect a stale reply instead of pre-paying for one

`I2cDevice::ReadRegister` now starts from the caller's response delay and
escalates towards 8 ms only when an attempt actually fails, so a healthy device
is not charged for a rare fault. The ANO's base delay is 1200 us -- four times
the 300 us the parameter encoders use successfully on the same bus.

The documented corruption is a position request answered with the preceding GPIO
reply, and that is directly detectable: a position word can never legitimately
equal the GPIO word. When it does, the read is retried once at the full 8 ms,
and a second identical result is reported as no movement rather than as an
error, so a transient cannot flap the connection status.

Expected budgets, healthy:

| worker | per sample | rate |
| --- | --- | --- |
| navigation | 2 reads x (1200 us + ~200 us wire) + 1 ms tail | ~3.8 ms, ~260 Hz |
| parameter encoders | 4 x 3 reads x (300 us + ~200 us wire) + 1 ms tail | ~7 ms, ~140 Hz |

The worst case also improved: a fully failing ANO read is now about 15 ms across
its three attempts, against 25 ms before.

### Buttons

No debounce value was changed, and none needed to be. A press must be seen by
two consecutive samples *and* survive the debounce interval, so at the old
~23 ms cadence a tap had to last 23-46 ms, and taps shorter than one sample
period could fall between samples entirely. Lowering the debounce from 10 ms to
4-6 ms, as the previous plan suggested, would have changed nothing at that
cadence, because 23 ms already exceeded both values. At ~3.8 ms sampling the
10 ms debounce is now the real constraint, which is what it was meant to be.

## Verification

Done off-device:

- `GpioNavigationPositionFilter` unit tests in `src/GpioTest.cpp`, covering every
  gesture in the table above in both directions, corrupt words while idle and
  mid-gesture, first-detent latency, and recovery from a stuck baseline. These
  were also run standalone against the class extracted verbatim from the header,
  at both a 4 ms and a 20 ms sample period, so the rules cannot regress into
  being speed-dependent.
- The threading and delay-escalation constructs were compile-checked in
  isolation, including that both workers now observe a refresh.

Not yet done -- the native daemon has not been compiled, because this project
builds on the Pi and no local Linux toolchain was available. **Build before
deploying.**

## Next session

1. Start from a clean `git status` and build the daemon on the Pi. Run the
   Catch2 tests if `catch/catch.hpp` is available there.
2. Measure the real cadence of both workers before tuning anything further. The
   estimates above are budgets, not measurements.
3. Run the quantitative tests: at least 100 clockwise and 100 anticlockwise
   detents, recording physical detents against UI navigation events; then 100
   presses of centre and each direction button, then a quick-press test.
4. Run the untouched soak with the OLED, matrix, and audio active.
5. If anticlockwise is still worse than clockwise, the cause is now most likely
   physical. Inspect or reflow COMA (centre button and rotary encoder), COMB
   (direction buttons), ENCA, and ENCB, and check connector strain and clearance
   from the metal enclosure.
6. The one number most worth tuning from measurements is the ANO base response
   delay of 1200 us in `SeesawNavigationEncoder::responseDelay`. If stale-reply
   retries show up often, raise it; if they never occur, it can come down.

### Still open

- Connecting the ANO interrupt pin to a Pi GPIO would allow event-driven
  sampling and a useful comparison against polling. Not attempted.
- Whether removing the INTFLAG short-press recovery for navigation in `b1d211b`
  was necessary is untested. The captured phantom was a corrupt encoder-position
  word, not a spurious interrupt flag, so it may have been collateral. At the
  new sampling cadence, ordinary sampling should catch short presses anyway, so
  this is probably moot -- confirm with the button tests before reopening it.

## Acceptance criteria

- At least 99/100 detected detents in each direction, ideally 100/100, without
  double steps.
- At least 99/100 normal presses for the centre and every direction button.
- A documented minimum quick-press duration that remains reliable.
- Zero spontaneous navigation events during a 30-minute untouched soak with the
  OLED, matrix, and audio active.
- No stale I2C responses, persistent I2C errors, or audio underruns caused by
  input polling.
- No loss of responsiveness or accuracy on the four parameter encoders.

## Completed hardware-interface work

### Navigation and parameter control

- All four standard encoders control the four parameters visible on the OLED.
  Turning adjusts a parameter; their push buttons are unassigned by default but
  remain mappable to parameters or functions.
- The ANO navigation encoder exclusively handles menu and display navigation.
- Navigation is layered as Settings -> Presets -> Effects -> Parameters.
- The wheel and Up/Down browse, Left moves outward, and Right moves inward
  without applying the highlighted preset or setting.
- Centre is the deliberate select/apply control in Settings and Presets. In
  Effects, Centre or Right opens the parameter view.
- Parameter browsing is one continuous sequence across every loaded effect.
- The hardware UI only exposes live input controls that are available in the
  PiPedal UI; hidden controls, outputs/meters, and bypass-only internals are
  excluded.

### OLED layout

- The parameter view displays four columns instead of two.
- Effect names form headers spanning their contiguous parameter columns, with
  boundary lines showing which parameters belong to each effect.
- Instructional `Press X` footers were removed to reclaim display space.
- Presets use a compact two-column, eight-item page with a slim vertical label
  and a selector shown only on the hovered row.
- Passive OLED modes include Controls, Waveform, Tuner, and Blank.

### LED matrix

- The HT16K33 display is treated as the visible rounded 5x5 area, excluding its
  hidden corner LEDs.
- Calibration supports origin, rotation, mirroring, and an asymmetric direction
  marker so the physical arrangement can be verified.
- Spectrum and Droplets display modes are available.
- Droplets follows the output peak/RMS envelope: note attacks create splashes,
  sustained notes generate smaller continuing ripples, and the visible LED count
  follows the envelope quickly.

### Reliability work already in place

- OLED and matrix rendering run separately from the input workers, and the OLED
  frame is sent in small chunks that yield the I2C bus between them.
- Encoder movement uses cumulative positions with I2C retry handling.
- The Pi I2C controller is configured for 400 kHz operation.

## Current hardware settings

- Parameter encoders: `0x3D`, `0x3B`, `0x38`, `0x39`; 1 ms configured polling and
  10 ms debounce.
- ANO navigation controller: `0x49`; 1 ms configured polling and 10 ms debounce.
- OLED: `0x3C`; 50 ms refresh; rotated 180 degrees; Controls passive mode.
- HT16K33: `0x71`; 16 ms refresh; brightness 15; Droplets mode; floor -55 dB;
  decay 0.65; origin `(1,2)`; rotation 2; mirrored.

Deployment records, installed binary checksums, and rollback file paths for this
installation are kept in the local working notes rather than here, so that this
repository stays free of host names, accounts, and build paths.

---
layout: default
title: GPIO Hardware Controls
---

# GPIO hardware controls

PiPedal can use Raspberry Pi GPIO buttons and switches, Linux IIO analog inputs, and Adafruit seesaw I²C rotary encoders. Hardware inputs are configured once, while their mappings are stored in each preset. A control can therefore adjust gain in one preset and delay feedback in another.

With the standard rig, all four ordinary encoders change effect parameters. A separate Adafruit ANO navigation wheel handles every screen and selection, including presets, effects, and the four-parameter window. An optional 128x64 SSD1306 I²C OLED shows all four parameters, and an Adafruit HT16K33 8x8 matrix can drive a rounded 5x5 output-spectrum display.

## Electrical safety

Raspberry Pi GPIO uses 3.3 V logic and is not 5 V tolerant. Never apply 5 V to a GPIO pin.

The Pi has no built-in analog input. Do not wire a potentiometer wiper directly to GPIO. Use a 3.3 V-compatible external ADC with a Linux IIO driver. PiPedal automatically lists the ADC's available IIO channels after the driver is enabled.

Disconnect power while changing wiring. Use appropriate protection and buffering for anything that leaves the enclosure or could carry static discharge.

## Typical switch wiring

The simplest normally-open button or footswitch needs two connections:

1. Connect one side of the switch to a GPIO pin.
2. Connect the other side to a Pi ground pin.
3. In PiPedal, select **Pull up** and **Active when low**.

This is also suitable for a maintained/latching switch. Select the matching input type so the mapping editor offers sensible behavior. A three-position switch can normally be represented as two latching inputs with a shared common connection.

The UI uses gpiochip line offsets. On the Pi's primary `/dev/gpiochip0`, these are the familiar BCM GPIO numbers, not physical header pin numbers. PiPedal shows the corresponding 40-pin header number where known.

## Configure inputs

Open **Settings → Hardware → GPIO / I2C buttons, encoders and display**.

For each input:

- Give it a recognizable name such as `Left footswitch` or `Expression`.
- Choose momentary, latching, Adafruit I²C encoder, or analog.
- For a switch, select its GPIO controller and BCM line, pull resistor, active level, and debounce time.
- For an analog input, select the discovered ADC channel and enter its measured raw minimum and maximum.
- Use smoothing to reduce noise and deadband to prevent tiny value changes from constantly updating an effect.

The live value at the bottom of each input card confirms the wiring and polarity before mappings are added. A line already owned by the kernel or another process is marked as in use.

PiPedal's installer adds the `pipedal_d` service account to Raspberry Pi OS's `gpio` group when that group exists. Restart the service or reboot after upgrading if the status reports a permission error.

## Four encoders, navigation, OLED, and LED matrix

The Hardware settings page has a **Set up four encoders + navigation + displays** button. It creates four Adafruit I²C QT Rotary Encoder inputs, an Adafruit ANO navigation input, and enables the OLED and LED matrix with these defaults:

| Device | I²C address | Address configuration |
| --- | --- | --- |
| Encoder 1 | `0x36` | no address jumper |
| Encoder 2 | `0x37` | bridge `A0` |
| Encoder 3 | `0x38` | bridge `A1` |
| Encoder 4 | `0x39` | bridge `A0` and `A1` |
| ANO navigation adapter | `0x49` | default address |
| PiicoDev OLED | `0x3C` | address switch off |
| Adafruit 8x8 LED backpack | `0x70` | no address jumper |

The encoder boards support addresses `0x36` through `0x3D`; each device on a bus must have a unique address. The OLED can use `0x3C` or `0x3D`, so keep it at `0x3C` when using the four-address layout above.
The mini 8x8 backpack has two address jumpers: bridging `A0` changes its
address from `0x70` to `0x71` (`A1` is `0x72`, and both are `0x73`).

The same setup button assigns the four parameter roles:

| Encoder | Standard role |
| --- | --- |
| Encoder 1 | Change parameter 1 (left) |
| Encoder 2 | Change parameter 2 |
| Encoder 3 | Change parameter 3 |
| Encoder 4 | Change parameter 4 (right) |

Roles are global and can be reassigned on any encoder card. The four push
buttons have no default action and remain available for preset-specific
mappings. The ANO wheel and its five switches are reserved for navigation.

### Enable and wire I²C

Enable the header I²C controller in Raspberry Pi OS before connecting the rig:

```sh
sudo raspi-config nonint do_i2c 0
sudo reboot
```

After reboot, `ls /dev/i2c-1` should succeed. PiPedal discovers available `/dev/i2c-*` buses; select `/dev/i2c-1` for the normal 40-pin header. Do not use HDMI/DDC buses such as `/dev/i2c-20` or `/dev/i2c-21` for this rig.

Connect the shared bus to Pi 3.3 V, ground, SDA (BCM GPIO 2, physical pin 3), and SCL (BCM GPIO 3, physical pin 5). The Adafruit and PiicoDev boards are Qwiic/STEMMA QT compatible and may be daisy chained. Power this combination from **3.3 V**, not 5 V. Adafruit recommends a 400 kHz I²C bus for the ANO adapter; add `i2c_arm_baudrate=400000` to `/boot/firmware/config.txt` when the bus still uses its Raspberry Pi OS 100 kHz default.

Multiple sockets on a passive I²C expansion HAT are normally parallel
connectors to the same controller, not separate buses. They share bandwidth and
still require unique addresses. Only entries that appear as distinct usable
`/dev/i2c-*` controllers are independent buses; Raspberry Pi HDMI/DDC
controllers such as `/dev/i2c-20` and `/dev/i2c-21` are not general-purpose
header buses.

## Navigation layers

The normal, deepest screen is **Parameters**. The wheel, Up, and Down only move
the highlight or parameter window. Left walks outward and Right walks inward
through this fixed stack without applying a highlighted setting or loading a
preset:

1. Parameters — choose four adjacent parameters across the complete loaded effect chain.
2. Effects — choose an effect in the loaded chain as a shortcut into that part of the parameter sequence.
3. Presets — browse presets; press the centre Select button to load the highlighted preset.
4. Settings — a deliberately small hardware menu for the passive OLED screen, LED animation, brightness, response floor, and calibration pattern; press Select to change the highlighted setting.

On Effects, either Right or Select opens the global parameter sequence at the
highlighted effect. On Presets and Settings, only the centre Select button
performs the highlighted action. This keeps exploratory Up/Down/Left/Right
navigation from changing the rig.

Navigation activity temporarily replaces the passive OLED screen. After the configured timeout the OLED returns to four parameters, waveform, tuner, or blank, without losing the current navigation layer.

Menu screens do not repeat button instructions. The preset browser uses two
columns with a narrow vertical **PRESET** label and puts the `>` marker only on
the active row, fitting eight presets on each page.

The installer adds `pipedal_d` to both the `gpio` and `i2c` groups when present. Reinstall the updated package and reboot before testing so the service receives its new group membership.

## Scroll through parameters

On the Parameters layer, the ANO wheel moves through every loaded effect's
parameters in chain and port order. Each click moves the four-parameter window
by one, crossing effect boundaries naturally, and the list wraps at the end.

Only parameters that the web interface itself gives you a control for are
included. Ports a plugin marks as not shown on its user interface, bypass
ports, and output-only meters are all left out, so an encoder cannot reach a
parameter you would never adjust by hand. PiPedal also requires a live control
value on the loaded effect before adding a parameter to the hardware sequence.

Encoders 1 through 4 change the matching OLED parameter. One click is one of the
parameter's own steps: between declared scale points for an enumerated control,
one unit for an integer, one declared step where a plugin declares them, and
otherwise a fraction of the range set by **Clicks per full range** in the
hardware settings. Logarithmic parameters move by a constant ratio, so a click
feels the same at both ends of a frequency control.

Where you are scrolled to is stored in the preset, as the parameter itself
rather than a position in the list. Adding, removing or reordering effects
therefore keeps the window on the parameter you chose. Selecting a different
effect moves the saved anchor to that effect when necessary. Save the preset to
keep the position across a restart.

## Map footswitches, pedals and push buttons

Open a preset and select the circuit-board icon in the main effect toolbar. This switches the lower panel to **Hardware controls for this preset**.

Choose **Add mapping**, then select an input and action. Inputs that the
standard workflow already uses are not offered: an encoder holding a parameter
role turns its own way, and the ANO navigation device is fully reserved. The
push buttons of all four parameter encoders are free, and are natural places
for effect on/bypass or snapshots. Supported actions include:

- effect parameters, input level, and output level;
- effect on/bypass;
- load a specific preset;
- next or previous preset;
- select, advance, or go back through snapshots;
- next or previous bank.

Add several mappings with the same input to control several targets together. Mappings are part of the current preset, so save the preset after editing them.

### Input behavior

- **Follow input position** maps the current switch or potentiometer position directly to the target. Use this for expression controls and maintained switches.
- **Toggle on each press** alternates between the two parameter values or toggles an effect.
- **Set maximum on press** sends the high value when a button is pressed. Preset, snapshot, and bank actions always run once on a press.
- **Move by a step on each click** adds or subtracts the configured amount from the current parameter value, clamps it to the mapping range, and is the normal encoder-turn behavior.

For parameter mappings, **Value at low/off** and **Value at high/on** set the exact target range. Put the larger value first to reverse the direction. A response curve of `1` is linear; values above `1` provide finer control near the low end, while values below `1` provide finer control near the high end.

### Encoder turns are relative events

PiPedal reads the seesaw encoder's lifetime position and compares it with the
last good sample, then emits one `+1` or `-1` event per detent. A failed host
read therefore cannot consume movement: the next successful sample catches up.
If several detents accumulated between polls, they are replayed as individual
unit events. Implausible transient words are retried and discarded before they
can reach an effect or the OLED.

This means assigning or changing a mapping can never jump a parameter to an
encoder's historical position.

### Encoder turn and push button

Each I²C encoder supplies two independently assignable controls:

- **Turn** changes a parameter relative to its current value. Set **Value per encoder click** to choose fine or coarse parameter adjustment; the incoming hardware event itself is always one unit. Enumerated LV2 parameters automatically move between their declared scale points, and integer parameters stay integral.
- **Push button** behaves like a normal momentary button, so it can toggle bypass, load a preset, select a snapshot, or run another action.

Turn is only offered on an encoder whose controller role is **Unassigned**. On
an encoder holding a standard role, only the push button can be mapped, and
only for the four parameter encoders. Mappings left over from an older
configuration that can no longer fire are removed when the preset is loaded.

## OLED behavior

The SSD1306 settings include:

- I²C bus and address (`0x3C` or `0x3D`);
- temporary preset/action message time;
- waveform view enabled/disabled and input/output source;
- passive screen (four parameters, waveform, tuner, or blank);
- OLED refresh interval and 180-degree rotation.

The parameter display has four compact columns. Each shows a knob indicator,
parameter name, and formatted value. Contiguous columns belonging to the same
effect share a centred effect header; when the window crosses an effect
boundary, each effect gets its own header and the dividing line extends into
the header row. A bar along the bottom edge shows how far through the complete
loaded parameter sequence you have
scrolled. Preset, effect, settings, and parameter navigation temporarily
replace the configured passive screen.

Turning any parameter encoder temporarily shows the parameter display even when
the waveform or tuner is the selected screen. After the configured temporary
message time, the OLED returns to the previously selected screen.

Choose the passive screen from the hardware settings page or the top navigation
layer: four parameters, live waveform, built-in chromatic strobe tuner, or blank.

The tuner analyses a lock-free copy of the main input and does not need a tuner effect in the current preset. It reuses the SuprTuner bass-first 18–500 Hz NSDF design, including low-B acquisition, nearest-note/cents output, and octave-normalized strobe motion. Analysis only runs while the tuner screen is selected. The audio path is never altered.

The waveform and tuner input are captured from real audio buffers through lock-free sample rings; they add no locks or allocation to the real-time audio callback. The OLED is refreshed at a deliberately modest rate so four encoder reads remain responsive on the shared I²C bus.

## Rounded 5x5 audio matrix

The HT16K33 renderer analyses the existing lock-free output waveform and offers two animations. **Five-band spectrum** draws low-to-high columns with peak decay. **Audio droplets** follows a fast peak/RMS envelope: a note attack makes an immediate centre splash, while a held note continuously excites smaller ripples at a level-dependent rate. The number of illuminated surface points follows the envelope and their positions follow a damped 5x5 wave simulation. A 16 ms refresh is the responsive default, with values down to 10 ms supported. Both animations run outside the realtime audio callback. Only the rounded 5x5 logical aperture is drawn; its four corner positions are always off.

Because an 8x8 matrix can be mounted in several orientations and the enclosure may expose either of the two central 5x5 positions, calibration includes X/Y origin, 90-degree rotation, and horizontal mirroring. Enable the asymmetric calibration arrow, adjust those settings until its point is at the physical top and its short tail is on the left, then turn calibration off. Animation, brightness, response floor, decay/ripple damping, and refresh rate are also configurable from the web page. Spectrum and Droplets can also be switched from the OLED Settings layer.

Turning an encoder or triggering a mapping draws immediately, because those show a timed overlay. A parameter changed from the web interface instead appears at the next scheduled refresh, so editing from a browser cannot flood the encoders' I²C bus with full-frame redraws. Lower **OLED refresh** if you want web edits reflected sooner.

## External ADC notes

PiPedal consumes standard Linux IIO `in_voltageN_raw` channels. This keeps ADC hardware support in the kernel, where device discovery and SPI/I²C coordination belong. After configuring the ADC in the Pi's boot/device-tree configuration, verify that a path similar to the following exists:

```
/sys/bus/iio/devices/iio:device0/in_voltage0_raw
```

Return to the GPIO settings screen and select the channel. Common ADC resolutions use raw ranges such as `0–1023` (10-bit), `0–4095` (12-bit), or `0–65535` (16-bit), but measuring the actual end positions gives better calibration.

## Troubleshooting

- **Permission denied:** reinstall/update PiPedal so `pipedal_d` is added to the `gpio` and `i2c` groups, then reboot.
- **Only `/dev/i2c-20` and `/dev/i2c-21` exist:** header I²C is not enabled; enable it with `raspi-config` and reboot.
- **An encoder is missing:** verify that no two devices share an address and check the A0/A1 solder bridges.
- **Clockwise decreases:** toggle **Reverse rotation** for that encoder.
- **OLED is blank:** verify 3.3 V power, select `0x3C` with its address switch off, and check that no encoder uses `0x3C`.
- **ANO navigation is missing:** its default address is `0x49`, not the `0x36` used by a QT parameter encoder; verify the input type and address together.
- **Navigation is sluggish:** configure the Raspberry Pi header bus for 400 kHz as described above. PiPedal still gives the ANO's ATtiny816 the 8 ms register-response time used by Adafruit's Linux driver; the four parameter encoders retain the faster response path.
- **Encoder turns or button taps are missed:** use the standard 1 ms poll and 10 ms encoder-button debounce settings, configure the header bus for 400 kHz, and keep SDA/SCL wiring short with a common ground. PiPedal polls inputs on a worker independent of OLED rendering and reads the seesaw's accumulated absolute position. Ordinary parameter-encoder buttons can recover short taps from latched GPIO activity; safety-critical navigation commands require the sampled pin to survive the full debounce interval, so electrical spikes cannot move through menus.
- **ANO controls differ in reliability:** inspect both common connections. `COMA` serves the centre switch and rotary encoder; `COMB` serves Up/Down/Left/Right. Nearby grounded metal should not electromagnetically block this mechanical part, but case contact, board flex, or an intermittent common/solder joint can interrupt those circuits.
- **LED matrix does not acknowledge:** with no jumpers use `0x70`; with `A0` bridged use `0x71`. Keep the Raspberry Pi wiring at 3.3 V unless a separately documented level shifter isolates 5 V from SDA/SCL.
- **LED matrix is rotated or shifted:** enable its calibration arrow and adjust origin, rotation, and mirror before returning to audio mode.
- **Line is busy:** choose another GPIO or disable the kernel feature currently using the line.
- **A button works backwards:** toggle **Active when low**.
- **A button fires more than once:** increase debounce from the default 30 ms.
- **A parameter moves too slowly or too coarsely:** change **Clicks per full range** in the hardware settings. Parameters that declare their own step size ignore it.
- **A parameter you expected is missing from the scroll:** its plugin marks that port as not shown on a user interface, so PiPedal will not let an encoder reach it either.
- **No ADC channels:** enable the external ADC's Linux IIO driver; the Pi itself provides none.
- **A potentiometer chatters:** increase smoothing and/or deadband.
- **Mappings disappeared:** mappings are preset data; save the preset after editing and configure each preset that should use them.

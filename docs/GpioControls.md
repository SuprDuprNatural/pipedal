---
layout: default
title: GPIO Hardware Controls
---

# GPIO hardware controls

PiPedal can use Raspberry Pi GPIO buttons and switches, Linux IIO analog inputs, and Adafruit seesaw I²C rotary encoders. Hardware inputs are configured once, while their mappings are stored in each preset. A control can therefore adjust gain in one preset and delay feedback in another.

An optional 128x64 SSD1306 I²C OLED normally shows the selected effect and two mapped parameters as a compact two-knob display. Encoder 2 switches between that control dashboard, a live input/output waveform, and a built-in bass-first strobe tuner.

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

## Four-encoder and OLED rig

The Hardware settings page has a **Set up four encoders + OLED** button. It creates four Adafruit I²C QT Rotary Encoder inputs and enables a PiicoDev SSD1306 display with these defaults:

| Device | I²C address | Address configuration |
| --- | --- | --- |
| Encoder 1 | `0x36` | no address jumper |
| Encoder 2 | `0x37` | bridge `A0` |
| Encoder 3 | `0x38` | bridge `A1` |
| Encoder 4 | `0x39` | bridge `A0` and `A1` |
| PiicoDev OLED | `0x3C` | address switch off |

The encoder boards support addresses `0x36` through `0x3D`; each device on a bus must have a unique address. The OLED can use `0x3C` or `0x3D`, so keep it at `0x3C` when using the four-address layout above.

The same setup button assigns a standard controller role to each encoder:

| Encoder | Standard role |
| --- | --- |
| Encoder 1 | Turn to browse presets; press to load the displayed preset |
| Encoder 2 | Turn to select an effect; press to cycle controls, waveform, and tuner OLED views |
| Encoder 3 | Change Parameter 1 for the selected effect |
| Encoder 4 | Change Parameter 2 for the selected effect |

Roles are global and can be reassigned on any encoder card. Parameter 1 and
Parameter 2 targets are saved per preset, so the same physical controls adapt
to every rig without inheriting a stale absolute knob position.

### Enable and wire I²C

Enable the header I²C controller in Raspberry Pi OS before connecting the rig:

```sh
sudo raspi-config nonint do_i2c 0
sudo reboot
```

After reboot, `ls /dev/i2c-1` should succeed. PiPedal discovers available `/dev/i2c-*` buses; select `/dev/i2c-1` for the normal 40-pin header. Do not use HDMI/DDC buses such as `/dev/i2c-20` or `/dev/i2c-21` for this rig.

Connect the shared bus to Pi 3.3 V, ground, SDA (BCM GPIO 2, physical pin 3), and SCL (BCM GPIO 3, physical pin 5). The Adafruit and PiicoDev boards are Qwiic/STEMMA QT compatible and may be daisy chained. Power this combination from **3.3 V**, not 5 V.

The installer adds `pipedal_d` to both the `gpio` and `i2c` groups when present. Reinstall the updated package and reboot before testing so the service receives its new group membership.

## Map controls in a preset

Open a preset and select the circuit-board icon in the main effect toolbar. This switches the lower panel to **Hardware controls for this preset**.

With the standard four-encoder roles enabled, each controllable effect has two
parameter selectors. Choose the desired Parameter 1 and Parameter 2 controls
and, if necessary, adjust the parameter step per click. Effects with neither
parameter assigned are skipped by Encoder 2.

Select **Show advanced mappings** to use the free-form mapping system. Choose
**Add advanced mapping**, then select an input and action. Supported actions include:

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

The seesaw encoder's cumulative position is deliberately not used as a control
value. PiPedal reads the hardware's delta-since-last-read register and emits
one `+1` or `-1` event per detent. If several detents accumulated between
polls, they are replayed as individual unit events. Implausible transient words
are retried and discarded before they can reach an effect or the OLED.

This means assigning or changing a mapping can never jump a parameter to an
encoder's historical position.

### Advanced encoder turn, push button, and selectors

Each I²C encoder supplies two independently assignable controls:

- **Turn** changes a parameter relative to its current value. Set **Value per encoder click** to choose fine or coarse parameter adjustment; the incoming hardware event itself is always one unit. Enumerated LV2 parameters automatically move between their declared scale points, and integer parameters stay integral.
- **Push button** behaves like a normal momentary button, so it can toggle bypass, load a preset, select a snapshot, or run another action.

An encoder can control several parameters simultaneously by leaving every mapping's **Selected by** field at **Always active**.

To make one encoder select what another encoder edits, add two or more parameter mappings for the controlled encoder and choose the same selector encoder in **Selected by** on each mapping. Turning the selector cycles that group; the OLED immediately shows the selected effect and parameter. Turning the controlled encoder changes only the selected mapping. The selector's push button remains available for an independent action.

Because mappings are stored in the pedalboard, both the assigned parameters and selector groups may be completely different in every preset.

## OLED behavior

The SSD1306 settings include:

- I²C bus and address (`0x3C` or `0x3D`);
- temporary preset/action message time;
- waveform view enabled/disabled and input/output source;
- OLED refresh interval and 180-degree rotation.

The control dashboard is the default screen. It shows the selected effect, both parameter labels and formatted values, and two minimal knob indicators that move as Encoder 3 or 4 changes a value. Preset browsing temporarily replaces it with the current preset in small text and the candidate preset in large text.

Turning Encoder 2, 3, or 4 temporarily shows the two-knob dashboard even when
the waveform or tuner is the selected screen. After the configured temporary
message time, the OLED returns to the previously selected screen.

Press Encoder 2 to cycle:

1. the two-knob control dashboard;
2. the live waveform;
3. the built-in chromatic strobe tuner.

The tuner analyses a lock-free copy of the main input and does not need a tuner effect in the current preset. It reuses the SuprTuner bass-first 18–500 Hz NSDF design, including low-B acquisition, nearest-note/cents output, and octave-normalized strobe motion. Analysis only runs while the tuner screen is selected. The audio path is never altered.

The waveform and tuner input are captured from real audio buffers through lock-free sample rings; they add no locks or allocation to the real-time audio callback. The OLED is refreshed at a deliberately modest rate so four encoder reads remain responsive on the shared I²C bus.

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
- **Line is busy:** choose another GPIO or disable the kernel feature currently using the line.
- **A button works backwards:** toggle **Active when low**.
- **A button fires more than once:** increase debounce from the default 30 ms.
- **No ADC channels:** enable the external ADC's Linux IIO driver; the Pi itself provides none.
- **A potentiometer chatters:** increase smoothing and/or deadband.
- **Mappings disappeared:** mappings are preset data; save the preset after editing and configure each preset that should use them.

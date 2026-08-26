---
layout: default
title: Raspberry Pi Codec Zero
---

# Raspberry Pi Codec Zero

PiPedal supports the Raspberry Pi Codec Zero as a full-duplex stereo audio device using its AUX IN and AUX OUT connections. USB audio interfaces remain supported and can be selected again at any time.

## Select it in PiPedal

Open **Settings -> Audio Device Settings**, then select **Raspberry Pi Codec Zero (Stereo AUX)** for both input and output. PiPedal recognizes both the current `Zero` ALSA card name and the older `IQaudIOCODEC` name.

Whenever audio starts, PiPedal programs the DA7212/DA7213 mixer for this path:

```
Stereo AUX IN -> input mixer -> ADC -> I2S -> PiPedal
PiPedal -> I2S -> DAC -> output mixer -> stereo AUX OUT
```

The initial analogue levels follow Raspberry Pi's reference stereo-AUX profile: AUX input at 0 dB, mixin PGA at +6 dB, ADC and DAC at 0 dB, and AUX/headphone output at -8 dB. DSP features such as ALC, EQ, noise gate, mono mixing and filtering are disabled so that PiPedal receives and produces an unprocessed stereo signal.

The routing is applied only to a detected Codec Zero. Selecting a USB interface follows the normal ALSA path and does not alter that device's mixer controls. PiPedal also supports using the Codec Zero for only one direction and another interface for the other, although using one clocked device for both directions is normally more reliable.

## Wiring to the Raspberry Pi header

Disconnect power before changing any wiring. The Codec Zero needs the standard HAT power, ground, control and audio signals. The four exclusive I2S audio connections are:

| Raspberry Pi signal | BCM GPIO | Physical pin | Direction relative to Pi |
| --- | ---: | ---: | --- |
| PCM clock / BCLK | 18 | 12 | Pi to codec |
| PCM frame sync / LRCLK | 19 | 35 | Pi to codec |
| PCM data in | 20 | 38 | Codec ADC to Pi |
| PCM data out | 21 | 40 | Pi to codec DAC |

The codec's control interface shares header I2C on GPIO 2/3 (physical pins 3/5). It also needs 3.3 V, 5 V and ground as provided by the HAT header, plus the HAT EEPROM ID pins 27/28 if EEPROM auto-detection is required.

Do not connect GPIO 20 and 21 according to a label such as "IN" or "OUT" without checking whose perspective the label uses: Pi `PCM_DIN` receives the codec's ADC data, while Pi `PCM_DOUT` feeds the codec's DAC.

The Codec Zero's on-board green LED, red LED and button use GPIO 23, 24 and 27 respectively. Do not assign conflicting external controls to those GPIOs while the fully attached HAT is installed.

## Coexistence with the I2C controls and displays

The encoders, ANO controller, OLED and LED matrix can remain on `/dev/i2c-1`. I2S is a separate hardware interface and does not put audio traffic on the I2C bus. The Codec Zero uses I2C address `0x1A` only for low-rate control-register access; that address does not conflict with the standard PiPedal layout (`0x36`-`0x39`, `0x49`, `0x3C` and `0x70`). A 400 kHz I2C bus is suitable.

GPIO 18-21 are exclusive to I2S and must not be assigned as ordinary PiPedal GPIO controls. While a Codec Zero is attached, PiPedal will refuse to open direct-GPIO controls on those four lines and will report the conflict in the GPIO input status. The saved control assignment is retained so that it can become available again if the HAT is removed. The HAT EEPROM is also exclusive on physical pins 27/28. The other I2C devices can share GPIO 2/3 normally as long as every device has a unique address and the wiring remains short and electrically sound.

## Raspberry Pi OS configuration

Current Codec Zero boards contain a HAT EEPROM, so Raspberry Pi OS normally loads the sound-card overlay automatically. Verify detection with:

```sh
aplay -l
arecord -l
```

Both should list `RPi Codec Zero`. If the EEPROM is not present or cannot be read, add this to `/boot/firmware/config.txt` and reboot:

```ini
dtoverlay=rpi-codeczero
```

Do not add the overlay when EEPROM auto-detection is already working. Older black IQaudIO-branded hardware may also require an empty `dtoverlay=` line before the explicit `rpi-codeczero` overlay; follow the current Raspberry Pi audio-board instructions for the detected vendor. The built-in Raspberry Pi headphone device may remain enabled because PiPedal selects devices by stable ALSA card id rather than card number.

## Levels and guitar input

AUX IN and AUX OUT are nominally 1 Vrms. AUX IN is a line-level input rather than a high-impedance guitar input, so place a proper buffer in front of it. A buffered tuner output is suitable. If hard playing clips the converter, reduce the signal before AUX IN; a compressor after the Codec Zero cannot undo ADC clipping. The -8 dB reference output setting leaves useful headroom, and downstream make-up gain can compensate when necessary.

## Kernel startup-delay limitation

Current Raspberry Pi kernels can impose an unconditional one-second Codec Zero DAPM delay during each ALSA `prepare`. At low latency this may cause one recovered xrun when audio first starts. PiPedal recovers it and, on a healthy system, the xrun does not repeat. Raspberry Pi is tracking the driver issue in [raspberrypi/linux #7525](https://github.com/raspberrypi/linux/issues/7525), with a mic-path-scoped fix proposed in [PR #7535](https://github.com/raspberrypi/linux/pull/7535).

PiPedal does not try to hide this by sleeping or increasing buffers in its real-time audio thread: either approach would add latency and synchronous recovery can compound the kernel delay. Until the kernel fix ships, treat a single recovered startup xrun as an upstream limitation; a continuing underrun/overrun loop is not expected and should be investigated.

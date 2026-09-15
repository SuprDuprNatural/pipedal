---
page_icon: img/Setup.jpg
icon_width: 320px
icon_float: left
---

## Configuring PiPedal after installation

Before using PiPedal, configure the audio device that it will use.

{% include pageIconL.html %}

You can also configure PiPedal to provide a Wi-Fi auto-hotspot for a phone,
tablet, or laptop. A normal Wi-Fi network is convenient at home; enable and
test the auto-hotspot before using PiPedal away from that network.

PiPedal uses LV2 audio plugins. Thousands of freely available LV2 plugins are
suitable for guitar effects.

By default, PiPedal comes with a few plugins from the ToobAmp plugin collection. You will probably want to install more.

See [Using LV2 plugins](UsingLv2Plugins.md) for details and recommended plugin
collections.

### Installing the PiPedal Remote Android app

For an Android phone or tablet, install the
[PiPedal Android client](https://play.google.com/store/apps/details?id=com.twoplay.pipedal).

The Android client provides three main features:

1. It locates PiPedal automatically on the local network using mDNS/Bonjour,
   so you do not need to remember an IP address.

2. It presents the PiPedal interface without a browser address bar or other
   browser controls.

3. It works with PiPedal's auto-hotspot for simple connections when no Wi-Fi
   router is available.

You can also use a mobile browser, but the Android client handles discovery
and hotspot connection automatically.

The client connects automatically when the Android device and Raspberry Pi are
on the same Wi-Fi network. Away from that network, PiPedal can start its own
hotspot:

- You show up at a local venue to perform.

- Power on the Raspberry Pi. If it cannot connect to a configured network,
  PiPedal starts its hotspot.

- The phone or tablet connects to the PiPedal hotspot.

- Launch the PiPedal client. It discovers the server over mDNS/Bonjour and
  opens the PiPedal interface.

## First connection

You can complete the initial configuration procedure using any of the following methods:

1. _On the Raspberry Pi._ Open a browser and visit <http://127.0.0.1/>.

2. _From a laptop or desktop._ Connect the Raspberry Pi to Wi-Fi or Ethernet,
   then visit <http://raspberrypi/>. Substitute its configured hostname if it
   is not `raspberrypi`.

3. _From Android._ Connect the Raspberry Pi to the same network and launch
   PiPedal Remote.

4. _From a Linux shell._ As a fallback, configure the hotspot with
   `pipedalconfig`; run `pipedalconfig --help` for its options.

If you already have another web server on port 80, see [*How to Change the Web Server Port*](ChangingTheWebServerPort.md).

The first connection opens the onboarding dialog. Select and configure the
audio device and, optionally, the Wi-Fi auto-hotspot.

### Configuring audio

Open the menu in the upper-left corner, choose **Settings**, then choose
**Audio device settings**.

The Raspberry Pi Codec Zero is available as **Raspberry Pi Codec Zero (Stereo AUX)** when its HAT has been detected. PiPedal configures its stereo AUX input/output routing automatically whenever it is selected. See [Raspberry Pi Codec Zero](CodecZero.md) for wiring and level guidance. External USB audio interfaces remain available in the same input and output device lists.

After selecting an audio device, choose the input and output channels used for
the instrument signal. Some two-input USB devices present the instrument on the
right channel, in which case select **Right only**. Devices with more channels
provide a channel list.

#### Selecting audio buffer sizes

Experiment to find a buffer size and count that suit the device. Round-trip
latency and xrun frequency depend on the operating system, audio hardware, and
CPU cost of the pedalboard. Larger buffers provide more processing time and
reduce dropouts; smaller buffers reduce latency but make xruns more likely.

Heavy display or storage activity and other CPU-intensive programs can cause
xruns. For best results, dedicate the Raspberry Pi to PiPedal. Connecting
remotely to the web interface does not normally cause problems.

PiPedal provides `pipedal_latency_test` to measure round-trip audio latency.
Stop the service temporarily with `sudo systemctl stop pipedald`, then connect
the audio device's left output to its left input with a suitable cable.

The following table shows measured round-trip audio latencies for a MOTU M2 external USB adapter running on Raspberry Pi OS. You can use these figures as a rough guideline; but actual round-trip audio latency will depend on the audio device you are using.

<table align='center'>
    <tr><td></td><td colspan=3>Buffers</td></tr>
    <tr><td>Size</td><td>2</td><td>3</td><td>4</td></tr>
    <tr><td>16</td><td>Fails</td><td>185/3.9ms</td><td>201/4.2ms</td></tr>
    <tr><td>24</td><td>192/4.0ms</td><td>213/4.4ms</td><td>236/4.9ms</td></tr>
    <tr><td>32</td><td>219/4.6ms</td><td>236/4.9ms</td><td>272/5.7ms</td></tr>
    <tr><td>48</td><td>253/5.3ms</td><td>299/6.2ms</td><td>348/7.2ms</td></tr>
    <tr><td>64</td><td>280/5.8ms</td><td>346/7.2ms</td><td>411/8.6ms</td></tr>
    <tr><td>128</td><td>442/9.2ms</td><td>571/11.9ms</td><td>699/14.6ms</td></tr>
</table>

Two buffers provide lower latency but leave little CPU time for audio
processing. Four buffers provide more headroom. A size of 16 increases overhead
because the system handles buffers more often, but 16×4 is a good starting
point when the audio device supports it.

Prefer 48 kHz over 44.1 kHz when the device supports it. The higher sample rate
helps plugins avoid high-frequency artifacts, and Neural Amp Models are
commonly designed for 48 kHz.

### Configuring Input Trim Levels on Older USB Audio Devices

For best results, you should set the input gain of your audio device so that the signal level is as high as possible without clipping. If you click on the 
input node of a preset, the left VU meter will show the audio input levels as received directly from your audio adapter. 

Some older USB audio devices do not provide volume knobs to control input gain of the audio signal, and the default trim settings are often less than 
ideal. To configure the input gain on these devices, use the following procedure. 

- ssh to the host, or launch a terminal window on the host if you have configured your Raspberry Pi to boot to a graphical desktop.
- Run `alsamixer` to configure the input gain.
- Press F6 to select the sound card. 
- Press TAB to scroll across pages/channels until you reach the CAPTURE slider(s). 
- Play the instrument while watching the input VU meter. Use the up and down
  arrow keys to adjust the input gain.
- Press the ESC key to close alsa mixer. 
- Run `sudo alsactl store` to save the settings permanently.



## Activating the Wi-Fi auto-hotspot

The PiPedal <b><i>Auto-Hotspot</i></b> feature allows you to connect to your Raspberry Pi even if you don't have
access to a Wi-Fi router. For example, if you are performing at a live venue, you probably will not
have access to a Wi-Fi router; but you can configure PiPedal so that your Raspberry Pi  automatically
starts a Wi-Fi hotspot when you are not at home. The feature is primarily intended for use with the
PiPedal Android client, but you may find it useful for other purposes as well.

A Raspberry Pi cannot normally host a hotspot and maintain another Wi-Fi
connection at the same time. The auto-hotspot can start when no preferred
network is available and stop when the Raspberry Pi should join another access
point. Choose the mode that matches the way you normally connect.

Open the auto-hotspot dialog from onboarding or from **Settings → Auto
hotspot**.

The dialog offers several activation modes:

If you normally use Ethernet, **No Ethernet connection** starts the hotspot
when the cable is unplugged. **Always on** is also available, but a phone may
then have to choose between the home network and the PiPedal hotspot.

If you normally use Wi-Fi, **Not at home** disables the hotspot while the
selected home network is visible and enables it when that network is absent.

If you use several regular networks, **No remembered Wi-Fi connections** starts
the hotspot only when none of them is visible. Use this mode carefully: a
remembered public network near a venue could prevent the hotspot from starting,
while client isolation on that network could also prevent direct access.

Test the chosen mode before taking PiPedal away from your normal network.

### Connecting from a laptop away from home

If your laptop has Wi-Fi support, you can use a laptop to connect to Raspberry Pi when you are away from home. 

Connect the Raspberry Pi and laptop to the same Wi-Fi router. Raspberry Pi OS
announces its hostname through mDNS/Bonjour, which is supported by Windows and
macOS.

Simply launch

    http://raspberrypi

(or supply the actual hostname of your Raspberry Pi if you have changed the default hostname).

Away from home, you can let the laptop provide a hotspot and configure the
Raspberry Pi to join it. After the first connection and credential setup,
Raspberry Pi OS should reconnect whenever that hotspot is available.

Use `http://raspberrypi` or the configured hostname;
`http://raspberrypi.local` may be required on Linux. Avahi provides
mDNS/Bonjour resolution on Linux when it is not already installed.

Alternatively, enable PiPedal's auto-hotspot and connect the laptop to it. Try
`http://raspberrypi` first. If hostname discovery is unavailable, open
<http://192.168.60.1> while connected to the PiPedal hotspot.


--------
[<< Headless Operation](HeadlessOperation.md)  | [Up](Documentation.md) | [What PiPedal Is >>](WhatPiPedalIs.md)

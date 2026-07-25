---
layout: default
title: AirPlay Receiver
---

# AirPlay receiver

PiPedal can act as an AirPlay receiver, so a phone, tablet or Mac can stream
backing tracks straight into the same outputs your guitar is coming out of. The
stream is mixed in *after* the pedalboard, so it is never processed by your
effects and never changes your tone.

Audio arrives through [shairport-sync](https://github.com/mikebrady/shairport-sync),
which PiPedal runs as `pipedal-airplay.service`. PiPedal itself reads the stream
from a FIFO and mixes it on the realtime audio thread.

## Install

shairport-sync is not installed by default:

```sh
sudo apt install shairport-sync
```

Nothing else needs configuring by hand. The first time you switch AirPlay on,
PiPedal writes `/etc/pipedal/shairport-sync.conf` and the
`pipedal-airplay.service` unit, then starts the receiver.

## Use it

The AirPlay switch and volume slider are in the main PiPedal toolbar. Turn the
switch on and PiPedal appears as an AirPlay speaker on your network, named after
your device name (the same name used for the PiPedal web address).

**Settings → Audio → AirPlay output** chooses where the stream goes:

- **Main outputs** mixes AirPlay into PiPedal's normal output, after the
  pedalboard. Use this when you have one stereo output.
- A specific **output pair** sends AirPlay to its own physical outputs on a
  multi-channel interface, so you can send backing tracks to a monitor mix
  without putting them in the front-of-house feed.

The volume slider only scales the AirPlay stream. Your pedalboard level is
unaffected.

## AirPlay always starts switched off

Whenever pipedald starts, the AirPlay receiver is off, regardless of how it was
left. Volume and output routing are remembered; only the on/off state is reset.

This is deliberate:

- a pedalboard should not advertise a network audio receiver that nobody asked
  for, on every stage and every rehearsal-room Wi-Fi it joins;
- a sender that was mid-session when the Pi lost power comes back to a clean
  receiver instead of a half-open one.

The service is intentionally left disabled in systemd for the same reason, so it
is never launched at boot ahead of PiPedal.

## Troubleshooting

**"AirPlay" does not appear on the sender.** Check that shairport-sync is
installed, that the switch is on, and that the sender is on the same network and
subnet. AirPlay discovery uses mDNS/Bonjour, which most guest and client-isolated
Wi-Fi networks block.

**A Mac will not reconnect after PiPedal was switched off.** Prefer switching
AirPlay off in PiPedal (or shutting the Pi down properly) rather than pulling the
power: that lets shairport-sync withdraw its Bonjour advertisement while the
network is still up, so the Mac learns the receiver is gone. PiPedal also
configures `allow_session_interruption`, which lets a returning sender take over
a session that was never torn down instead of being refused as busy.

If a Mac still refuses to reconnect, the stale state is on the Mac, not on the
Pi — macOS caches AirPlay device state in `coreaudiod`. You do not need to
reboot; restarting that daemon is enough:

```sh
sudo killall coreaudiod
```

Audio on the Mac cuts out for a second or two and the receiver becomes selectable
again.

**Audio stutters or drops.** AirPlay adds about 100 ms of buffering before
playback starts, and skips ahead if more than about a second accumulates. Steady
dropouts usually mean Wi-Fi congestion; a wired connection to the Pi is the
reliable fix.

**Audio plays but nothing is audible.** Check the AirPlay output selection in
Settings → Audio. If it is routed to an output pair your interface does not have,
or one that is not connected to anything, the stream is being mixed into silence.

## Notes

shairport-sync streams 44100 Hz stereo into the FIFO. When your audio device runs
at a different rate, PiPedal resamples with Catmull-Rom interpolation on the FIFO
reader thread, not on the realtime thread.

The stock `shairport-sync.service` is disabled when PiPedal configures AirPlay.
Leaving it running would advertise a second receiver pointing at the ALSA device
PiPedal already owns.

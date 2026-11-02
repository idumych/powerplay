# PowerPlay

An audio player for Haiku with a small analog VU meter that can sit in the
Deskbar. Click the meter for play controls.

PowerPlay is a new program inspired by SoundPlay, the BeOS player by Marco
Nelissen. It shares no code, images or text with it; the meter's look was
measured from screenshots.

## Status

| Part | State |
|---|---|
| Needles: lively (3 ms up, 120 ms back; 0 VU = a -12 dBFS sine) by default, or classic VU (300 ms; 0 VU = -18 dBFS RMS, EBU R68) | done, host tests; checked against a real song |
| Meter face drawn pixel by pixel, matched to the original | done, host tests; checked on Haiku R1/beta6 |
| Deskbar meter with a play-controls menu | runs on Haiku R1/beta6 |
| Playing a file (BMediaFile + BSoundPlayer) | WAV played on Haiku R1/beta6 in QEMU (emulated HDA, output captured): full length, no dropouts, level unchanged |
| Vector icon (HVIF, tools/make-icon.py) | done; checked in Tracker, Deskbar and About on Haiku R1/beta6 |
| Playlist, main window like SoundPlay's | not started |
| Built-in Power Station skin (optional) | done, host test; seen on Linux (X11) and Haiku R1/beta6 |
| Winamp 2.x skins for the main window (.wsz or a folder; Skins menu) | done, host tests; the four test skins checked on Linux (X11, clicks, sliders, drag, relaunch) and drawn on Haiku R1/beta6; no playlist or equalizer skins, no region.txt shapes yet |
| Linux (Hokku) | builds unchanged against Hokku 29097935 and opens under Xvfb (X11): window, meter, About; no Deskbar on Linux yet; sound not tried |

## Building

On Haiku: `make`, then `./PowerPlay [file]`. Host tests on any system: `make test`.

## Skins

PowerPlay opens in its own SoundPlay-style window. From the Skins menu it
can instead look like a classic Winamp 2.x player, either in its own skin,
Power Station (a brushed-steel panel with amber displays, status lamps, push
buttons, LED faders and a hazard-striped kick plate, drawn by
`tools/make-skin.py` into `data/PowerStation.wsz` and carried as a
resource), or in any other Winamp 2.x skin; "PowerPlay window" goes back.

To add skins, put `.wsz` files (or folders of unpacked ones) in `~/config/settings/PowerPlay/Skins`
(`~/.config/cosmoe/PowerPlay/Skins` on Linux), or drop a `.wsz` on the
player to copy it there, then choose it from the Skins menu. Right-click a
skin for the menu. The keys are Winamp's: z previous, x play, c pause,
v stop, b next. PowerPlay comes with no skins but its own.

## Scripting

`hey PowerPlay get Levels` returns the needles' deflection
(`left=0.3981 right=0.3981 playing=1` for a -20 dBFS sine; 0.708 is 0 VU, which is
-18 dBFS RMS, and 1.0 the right end of the scale, +3 VU).

## Licence

MIT.

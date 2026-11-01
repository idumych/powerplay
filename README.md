# PowerPlay

An audio player for Haiku with a small analog VU meter that can sit in the
Deskbar. Click the meter for play controls.

PowerPlay is a new program inspired by SoundPlay, the BeOS player by Marco
Nelissen. It shares no code, images or text with it; the meter's look was
measured from screenshots.

## Status

| Part | State |
|---|---|
| VU ballistics (300 ms, calibrated to the test tones) | done, host tests |
| Meter face drawn pixel by pixel, matched to the original | done, host tests; checked on Haiku R1/beta6 |
| Deskbar meter with a play-controls menu | runs on Haiku R1/beta6 |
| Playing a file (BMediaFile + BSoundPlayer) | WAV played on Haiku R1/beta6 in QEMU (emulated HDA, output captured): full length, no dropouts, level unchanged |
| Playlist, main window like SoundPlay's | not started |
| Linux (Hokku) | builds unchanged against Hokku 29097935 and opens under Xvfb (X11): window, meter, About; no Deskbar on Linux yet; sound not tried |

## Building

On Haiku: `make`, then `./PowerPlay [file]`. Host tests on any system: `make test`.

## Scripting

`hey PowerPlay get Levels` returns the needles' deflection
(`left=0.7079 right=0.7079 playing=1`; 1.0 = full scale = +3 VU).

## Licence

MIT.

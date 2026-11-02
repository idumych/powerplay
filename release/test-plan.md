# Test plan: PowerPlay

## Fixtures
Test tones, made on the host: `python3 -I tools/mkwav.py <dir>` writes 1 kHz
sines at 0, -3, -6, -10 and -20 dBFS, a left-only -6 dBFS tone and a 120 BPM
pulse (20 s each, 44.1 kHz 16-bit stereo). Copy them to `~/sp` in the guest.
Playback needs a sound card in the guest: on Haiku, the QEMU machine from
`qemu-vm.sh` with `AUDIO=wav` (the guest's output lands in `$DIR/audio.wav`).

## Fresh state
Haiku: `rm -f ~/config/settings/PowerPlay_settings`; remove the meter from the
Deskbar (`hey PowerPlay TDsk` while it shows, or its menu's "Remove from Deskbar").
Linux: `rm -f ~/.config/cosmoe/PowerPlay_settings`.

## Feature checks
### play-tone
Steps: `PowerPlay ~/sp/tone-20dB.wav`, wait 2 s, `hey PowerPlay get Levels > {p}-play-tone.txt`.
PASS when: both needles read -5 VU, deflection 0.398 ±0.01 (`left=0.39… right=0.39…`;
0 VU is -18 dBFS RMS, so a -20 dBFS sine reads 5 dB under it), and the captured audio
has a 1 kHz tone at -20 dBFS (`sine-check.py`).

### deskbar-meter
Steps: Settings ▸ "Show meter in Deskbar"; screenshot; click the meter.
PASS when: a 48×16 meter shows in the Deskbar tray, its needles move with the
tone, and clicking it opens a menu with "Pause", "Stop", "Show PowerPlay" and
"Remove from Deskbar". Haiku only: Linux has no Deskbar (see README).

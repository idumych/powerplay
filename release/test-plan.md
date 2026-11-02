# Test plan: PowerPlay

## Fixtures
Test tones, made on the host: `python3 -I tools/mkwav.py <dir>` writes 1 kHz
sines at 0, -3, -6, -10 and -20 dBFS, a left-only -6 dBFS tone and a 120 BPM
pulse (20 s each, 44.1 kHz 16-bit stereo), and `tagged-tone.wav`: the -20 dBFS
tone with an ID3v1 tag (title "Signal Check", artist "Test Bench", album
"Calibration"). Copy them to `~/sp` in the guest.
Playback needs a sound card in the guest: on Haiku, the QEMU machine from
`qemu-vm.sh` with `AUDIO=wav` (the guest's output lands in `$DIR/audio.wav`).

## Fresh state
Haiku: `rm -f ~/config/settings/PowerPlay_settings`; remove the meter from the
Deskbar (`hey PowerPlay TDsk` while it shows, or its menu's "Remove from Deskbar").
Linux: `rm -f ~/.config/cosmoe/PowerPlay_settings`.

## Feature checks
### play-tone
Steps: `PowerPlay ~/sp/tone-20dB.wav`, `hey PowerPlay TLiv` (the classic VU
meter: "Lively needles" is on by default and reads differently),
`hey PowerPlay Volu with volume='float(1.0)'` (the meter reads what is played,
after the volume, 80% by default), wait 2 s,
`hey PowerPlay get Levels > {p}-play-tone.txt`. On Linux under Xvfb, start
the AppImage with `COSMOE_AUDIO_BACKEND=null` (no sound server; the meter still
reads what would be played).
PASS when: both needles read -5 VU, deflection 0.398 ±0.01 (`left=0.39… right=0.39…`;
0 VU is -18 dBFS RMS, so a -20 dBFS sine reads 5 dB under it), and the captured audio
has a 1 kHz tone at -20 dBFS (`sine-check.py`).

### skins
Steps: with the tone playing, `hey PowerPlay Skin with path="built-in:Power Station"`,
`hey PowerPlay ShEq`, `hey PowerPlay ShPl`; screenshot the three skin
windows; `ocr-find.sh --text` of the shot (3x enlarged crop and screen
scale) `> {p}-skins.txt`.
PASS when: the shot shows Power Station's main window, equalizer and
playlist, and OCR reads the tone's playlist entry (`tone-20dB`), drawn in
the skin's playlist window.

### library
Steps: quit, then `PowerPlay ~/sp/tagged-tone.wav`,
`hey PowerPlay Skin with path="look:Library"`, wait 3 s; screenshot the Library
window; `ocr-find.sh --text` of the shot (3x enlarged crop and screen scale)
`> {p}-library.txt`.
PASS when: the shot shows the Library window (Source list with Library and
Playlist; Song, Time, Artist and Album columns), and OCR reads the song's
title, artist and album from its tag (`Signal Check`, `Test Bench`,
`Calibration`; the file is named tagged-tone) and its length (`00:20`).

### deskbar-meter (Haiku; recorded, not gated)
Steps: `hey PowerPlay TDsk`; screenshot the Deskbar.
PASS when: a 48×16 meter shows in the Deskbar tray and its needles stand off
their rest while the tone plays. On Linux the meter goes to Hokku's Deskbar or
the desktop's tray, which the AppImage test machines don't have.

import math, struct, sys, wave, os
out = sys.argv[1]; R = 44100
def write(name, secs, fn):
    w = wave.open(os.path.join(out, name), "wb"); w.setnchannels(2); w.setsampwidth(2); w.setframerate(R)
    buf = bytearray()
    for i in range(int(secs * R)):
        l, r = fn(i / R)
        buf += struct.pack("<hh", int(max(-1, min(1, l)) * 32767), int(max(-1, min(1, r)) * 32767))
    w.writeframes(bytes(buf)); w.close()
s = lambda t, f: math.sin(2 * math.pi * f * t)
for db in (0, -3, -6, -10, -20):
    a = 10 ** (db / 20)
    write(f"tone{db:+d}dB.wav", 20, lambda t, a=a: (a * s(t, 1000), a * s(t, 1000)))
write("left-only-6dB.wav", 20, lambda t: (0.5 * s(t, 1000), 0.0))
def pulse(t):   # 120 BPM: decaying bursts, right channel half a beat later and quieter
    el = math.exp(-6 * (t % 0.5)); er = math.exp(-6 * ((t + 0.25) % 0.5))
    return 0.9 * el * s(t, 220), 0.6 * er * s(t, 330)
write("pulse.wav", 60, pulse)

# SDL1 audio device: open AC97 hardware directly.
# SDL1 tries S16_BE first; hw:0,0 rejects it, SDL falls back to S16_LE and
# builds an internal S16_BE→S16_LE converter. The emulator's mem_read16
# byteswaps back, so the two swaps cancel and audio is correct.
# plughw:0,0 fails to open (root cause unknown), so use hw:0,0 directly.
export AUDIODEV=hw:0,0

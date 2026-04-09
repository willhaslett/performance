#!/usr/bin/env python3
"""Send MIDI notes in a continuous loop until killed.

Usage: uvx --from python-rtmidi python3 tools/send_notes_loop.py [note] [velocity] [interval]
"""
import rtmidi
import time
import sys

note = int(sys.argv[1]) if len(sys.argv) > 1 else 60
velocity = int(sys.argv[2]) if len(sys.argv) > 2 else 100
interval = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5

midi_out = rtmidi.MidiOut()
midi_out.open_virtual_port("Test MIDI")
print(f"Sending note {note} vel={velocity} every {interval}s — Ctrl+C to stop")

try:
    while True:
        midi_out.send_message([0x90, note, velocity])
        time.sleep(0.15)
        midi_out.send_message([0x80, note, 0])
        time.sleep(interval - 0.15)
except KeyboardInterrupt:
    print("\nStopped")
finally:
    midi_out.close_port()

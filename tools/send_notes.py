#!/usr/bin/env python3
"""Send test MIDI notes via a virtual port. The Performance app will pick it up.

Usage: uvx --from python-rtmidi python3 tools/send_notes.py [note] [velocity] [count] [interval]

Start this FIRST, then launch/restart the app so it detects the 'Test MIDI' port.
Press Enter to send a burst of notes. Ctrl+C to quit.
"""
import rtmidi
import time
import sys

note = int(sys.argv[1]) if len(sys.argv) > 1 else 60
velocity = int(sys.argv[2]) if len(sys.argv) > 2 else 100
count = int(sys.argv[3]) if len(sys.argv) > 3 else 4
interval = float(sys.argv[4]) if len(sys.argv) > 4 else 0.3

midi_out = rtmidi.MidiOut()
midi_out.open_virtual_port("Test MIDI")
print(f"Virtual MIDI port 'Test MIDI' open.")
print(f"Restart the Performance app, create a track + instrument, then press Enter here.")
print(f"Will send: note={note}, vel={velocity}, count={count}, interval={interval}s")
print(f"Press Enter to send notes, Ctrl+C to quit.\n")

try:
    while True:
        input("Press Enter to send notes...")
        for i in range(count):
            midi_out.send_message([0x90, note, velocity])
            time.sleep(0.15)
            midi_out.send_message([0x80, note, 0])
            time.sleep(interval - 0.15)
        print(f"  Sent {count} notes.")
except KeyboardInterrupt:
    print("\nDone")
finally:
    midi_out.close_port()

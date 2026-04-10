#pragma once
#include <atomic>
#include <cstdint>

// Lock-free single-producer single-consumer ring buffer for MIDI recording.
// The audio thread pushes events with sample-accurate beat timestamps.
// The message thread drains them into the Arrangement.

struct RecordedMidiEvent {
    int noteNumber = 0;
    int velocity = 0;    // 0 = note-off
    int channel = 1;
    double beat = 0.0;   // absolute beat position
};

class RecordFIFO {
public:
    static constexpr int capacity = 1024;

    bool push(const RecordedMidiEvent& event) {
        int w = writePos.load(std::memory_order_relaxed);
        int next = (w + 1) % capacity;
        if (next == readPos.load(std::memory_order_acquire))
            return false;  // full
        buffer[w] = event;
        writePos.store(next, std::memory_order_release);
        return true;
    }

    bool pop(RecordedMidiEvent& event) {
        int r = readPos.load(std::memory_order_relaxed);
        if (r == writePos.load(std::memory_order_acquire))
            return false;  // empty
        event = buffer[r];
        readPos.store((r + 1) % capacity, std::memory_order_release);
        return true;
    }

    bool isEmpty() const {
        return readPos.load(std::memory_order_acquire)
            == writePos.load(std::memory_order_acquire);
    }

private:
    RecordedMidiEvent buffer[capacity];
    std::atomic<int> readPos { 0 };
    std::atomic<int> writePos { 0 };
};

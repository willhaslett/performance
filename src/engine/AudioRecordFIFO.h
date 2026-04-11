#pragma once
#include <atomic>
#include <algorithm>
#include <cstring>

// Lock-free single-producer single-consumer ring buffer for audio recording.
// Audio thread pushes interleaved stereo float samples.
// Writer thread drains them to disk.
// Capacity: ~2MB = ~5 seconds at 48kHz stereo.

class AudioRecordFIFO {
public:
    static constexpr int capacity = 524288;  // ~5.4 sec at 48kHz stereo

    // Push interleaved samples. Returns number of samples actually written.
    int push(const float* data, int numSamples) {
        int w = writePos.load(std::memory_order_relaxed);
        int r = readPos.load(std::memory_order_acquire);
        int available = (r - w - 1 + capacity) % capacity;
        int toWrite = std::min(numSamples, available);
        if (toWrite <= 0) return 0;

        int firstChunk = std::min(toWrite, capacity - w);
        std::memcpy(buffer + w, data, firstChunk * sizeof(float));
        if (toWrite > firstChunk)
            std::memcpy(buffer, data + firstChunk, (toWrite - firstChunk) * sizeof(float));

        writePos.store((w + toWrite) % capacity, std::memory_order_release);
        return toWrite;
    }

    // Pop samples into destination buffer. Returns number of samples read.
    int pop(float* dest, int maxSamples) {
        int r = readPos.load(std::memory_order_relaxed);
        int w = writePos.load(std::memory_order_acquire);
        int available = (w - r + capacity) % capacity;
        int toRead = std::min(maxSamples, available);
        if (toRead <= 0) return 0;

        int firstChunk = std::min(toRead, capacity - r);
        std::memcpy(dest, buffer + r, firstChunk * sizeof(float));
        if (toRead > firstChunk)
            std::memcpy(dest, buffer + firstChunk, (toRead - firstChunk) * sizeof(float));

        readPos.store((r + toRead) % capacity, std::memory_order_release);
        return toRead;
    }

    bool isEmpty() const {
        return readPos.load(std::memory_order_acquire)
            == writePos.load(std::memory_order_acquire);
    }

    void reset() {
        readPos.store(0, std::memory_order_release);
        writePos.store(0, std::memory_order_release);
    }

private:
    float buffer[capacity];
    std::atomic<int> readPos { 0 };
    std::atomic<int> writePos { 0 };
};

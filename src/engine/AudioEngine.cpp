#include "engine/AudioEngine.h"

AudioEngine::AudioEngine() {}

AudioEngine::~AudioEngine() {
    shutdown();
}

void AudioEngine::initialise() {
    auto result = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (result.isNotEmpty()) {
        DBG("Audio device error: " + result);
        return;
    }

    deviceManager.addAudioCallback(this);

    if (auto* device = deviceManager.getCurrentAudioDevice()) {
        DBG("Audio device: " + device->getName());
        DBG("Sample rate: " + juce::String(device->getCurrentSampleRate()));
        DBG("Buffer size: " + juce::String(device->getCurrentBufferSizeSamples()));
    }
}

void AudioEngine::shutdown() {
    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
}

void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& context) {
    // Clear output for now
    for (int ch = 0; ch < numOutputChannels; ++ch)
        juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    sampleRate = device->getCurrentSampleRate();
    bufferSize = device->getCurrentBufferSizeSamples();
    DBG("Audio started: " + juce::String(sampleRate) + " Hz, " + juce::String(bufferSize) + " samples");
}

void AudioEngine::audioDeviceStopped() {
    DBG("Audio stopped");
}

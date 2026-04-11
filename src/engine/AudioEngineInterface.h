#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

// Abstract interface for the subset of AudioEngine that EngineSync uses.
// Real AudioEngine implements this. Tests can use a mock.

class AudioEngineInterface {
public:
    virtual ~AudioEngineInterface() = default;

    using LoadCallback = std::function<void()>;

    // Track management
    virtual void createTrackWithId(const juce::String& id, const juce::String& name) = 0;
    virtual void createAudioInputTrackWithId(const juce::String& id, const juce::String& name,
                                              int inputChannelStart, int inputChannelCount) = 0;
    virtual void removeTrack(const juce::String& trackId) = 0;
    virtual bool addTrackInstrument(const juce::String& trackId, const juce::String& pluginName,
                                     LoadCallback onLoaded = nullptr) = 0;
    virtual void removeTrackInstrument(const juce::String& trackId) = 0;
    virtual void setTrackGain(const juce::String& trackId, float gain) = 0;
    virtual void setTrackMidiEnabled(const juce::String& trackId, bool enabled) = 0;
    virtual void setTrackAudioEnabled(const juce::String& trackId, bool enabled) = 0;
    virtual void setTrackInputChannels(const juce::String& trackId, int start, int count) = 0;
    virtual void renameTrack(const juce::String& trackId, const juce::String& newName) = 0;
    virtual void setTrackOutputTarget(const juce::String& trackId, const juce::String& target) = 0;
    virtual void clearAllTracks() = 0;

    // Bus management
    virtual void createBusWithId(const juce::String& id, const juce::String& name) = 0;
    virtual void removeBus(const juce::String& busId) = 0;
    virtual void setBusGain(const juce::String& busId, float gain) = 0;
    virtual void setBusAudioEnabled(const juce::String& busId, bool enabled) = 0;
    virtual void renameBus(const juce::String& busId, const juce::String& newName) = 0;
    virtual void setBusOutputTarget(const juce::String& busId, const juce::String& target) = 0;
    virtual void clearAllBusses() = 0;

    // Effects
    virtual bool addEffect(const juce::String& parentId, const juce::String& effectId,
                           const juce::String& pluginName, LoadCallback onLoaded = nullptr) = 0;
    virtual void removeEffect(const juce::String& parentId, const juce::String& effectId) = 0;

    // Sends
    virtual void addSend(const juce::String& trackId, const juce::String& busId, float gain = 1.0f) = 0;
    virtual void setSendGain(const juce::String& trackId, const juce::String& busId, float gain) = 0;

    // Master
    virtual void setMasterGain(float gain) = 0;
    virtual void setMasterAudioEnabled(bool enabled) = 0;

    // Query
    virtual std::vector<juce::String> getInputChannelNames() const = 0;
    virtual juce::String getTrackPluginName(const juce::String& trackId) const = 0;
    virtual juce::AudioProcessor* getTrackInstrumentProcessor(const juce::String& trackId) const = 0;
    virtual juce::AudioProcessor* getEffectProcessor(const juce::String& parentId,
                                                      const juce::String& effectId) const = 0;
};

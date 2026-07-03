#include "rendering/OfflineRenderer.h"
#include "engine/AudioEngine.h"
#include "engine/GraphWrapper.h"
#include "engine/Log.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

// Toggle setNonRealtime on every processor in the graph. JUCE's graph nodes
// wrap AudioProcessors; the graph itself has the flag too.
void setGraphNonRealtime(juce::AudioProcessorGraph& graph, bool nonRealtime) {
    graph.setNonRealtime(nonRealtime);
    for (auto* node : graph.getNodes()) {
        if (auto* proc = node->getProcessor())
            proc->setNonRealtime(nonRealtime);
    }
}

} // namespace

OfflineRenderer::Result OfflineRenderer::render(AudioEngine& engine,
                                                 const juce::File& outputFile,
                                                 double startBeat,
                                                 double endBeat,
                                                 double tempoBpm) {
    Result result;

    if (endBeat <= startBeat) {
        result.errorMessage = "endBeat must be greater than startBeat";
        return result;
    }
    if (tempoBpm <= 0.0) {
        result.errorMessage = "tempoBpm must be positive";
        return result;
    }

    auto& graph = engine.getGraph();
    auto& wrapper = engine.getGraphWrapper();
    // Render at the project's ENGINE rate, not the live device rate — export
    // quality must not depend on whatever output device is plugged in (e.g.
    // bouncing while on a 16k SCO Bluetooth device must still produce 48k).
    // The graph is re-prepared here for the render; resumeDeviceProcessing()
    // re-preps it back to the device rate afterward, so this is self-restoring.
    double sampleRate = engine.getEngineSampleRate();
    if (sampleRate <= 0.0) sampleRate = engine.getCurrentSampleRate();
    if (sampleRate <= 0.0) {
        result.errorMessage = "engine has no sample rate (device not open?)";
        return result;
    }

    const int blockSize = 512;  // chosen independently of device buffer size
    const double beatsPerSecond = tempoBpm / 60.0;
    const double durationSeconds = (endBeat - startBeat) / beatsPerSecond;
    const int64_t totalFrames = (int64_t)std::ceil(durationSeconds * sampleRate);

    perfLog("[OfflineRender] Start: file=%s beats=[%.3f, %.3f] tempo=%.1f "
            "sr=%.0f frames=%lld\n",
            outputFile.getFullPathName().toRawUTF8(),
            startBeat, endBeat, tempoBpm, sampleRate, totalFrames);

    outputFile.deleteFile();
    auto stream = outputFile.createOutputStream();
    if (!stream) {
        result.errorMessage = "could not open output file for writing";
        return result;
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.release(), sampleRate, 2, 24, {}, 0));
    if (!writer) {
        result.errorMessage = "could not create WAV writer";
        return result;
    }

    // Prepare the graph for our block size (may differ from the device's).
    // We call prepareToPlay on the wrapper to reset any sample-counting state
    // and ensure every node is configured for this block size.
    const int graphNumInputs  = graph.getTotalNumInputChannels();
    const int graphNumOutputs = std::max(2, graph.getTotalNumOutputChannels());
    wrapper.setPlayConfigDetails(graphNumInputs, graphNumOutputs, sampleRate, blockSize);
    wrapper.prepareToPlay(sampleRate, blockSize);

    setGraphNonRealtime(graph, true);

    // Start transport at the requested beat and tempo. The wrapper's
    // processBlock will advance based on its internal sample counter.
    wrapper.startPlayback(startBeat, tempoBpm, /*loopOn*/false, 0.0, 0.0);

    // Allocate buffer for graph IO. Graph may have inputs; we feed silence.
    const int numChannels = std::max(graphNumInputs, graphNumOutputs);
    juce::AudioBuffer<float> buffer(numChannels, blockSize);
    juce::MidiBuffer midi;

    // Separate buffer for the WAV output (2ch stereo, taken from first two
    // channels of the graph output).
    juce::AudioBuffer<float> outputBuffer(2, blockSize);

    const int64_t t0 = juce::Time::getMillisecondCounterHiRes();
    int64_t framesWritten = 0;

    while (framesWritten < totalFrames) {
        const int thisBlock = (int)std::min((int64_t)blockSize, totalFrames - framesWritten);

        buffer.clear();
        midi.clear();

        // Resize the buffer window for a potentially shorter last block.
        if (thisBlock != buffer.getNumSamples()) {
            // Keep the allocation; just process the first thisBlock samples.
            // JUCE's AudioBuffer doesn't easily resize without realloc, so
            // we clear and process the full block, then write just thisBlock.
        }

        // Process one block through the wrapper (which handles MIDI
        // scheduling from the arrangement + drives the graph).
        wrapper.processBlock(buffer, midi);

        // Copy the first two channels of graph output to our stereo output.
        const int srcCh0 = 0;
        const int srcCh1 = std::min(1, buffer.getNumChannels() - 1);
        outputBuffer.copyFrom(0, 0, buffer, srcCh0, 0, thisBlock);
        outputBuffer.copyFrom(1, 0, buffer, srcCh1, 0, thisBlock);

        writer->writeFromAudioSampleBuffer(outputBuffer, 0, thisBlock);
        framesWritten += thisBlock;
    }

    wrapper.stopPlayback();
    setGraphNonRealtime(graph, false);
    writer.reset();  // flush + close

    const double wallClock = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;

    result.ok = true;
    result.framesWritten = framesWritten;
    result.audioDurationSeconds = (double)framesWritten / sampleRate;
    result.wallClockSeconds = wallClock;

    perfLog("[OfflineRender] Done: %lld frames (%.2fs audio) in %.2fs wall (%.1fx realtime)\n",
            framesWritten, result.audioDurationSeconds, wallClock,
            result.audioDurationSeconds / std::max(wallClock, 0.001));

    return result;
}

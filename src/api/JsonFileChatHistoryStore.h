#pragma once
#include "api/ChatHistoryStore.h"
#include <juce_core/juce_core.h>

// JSON-file-backed ChatHistoryStore. Single file, full rewrite per
// save (atomic via write-temp-then-rename). Schema is internal — if
// the format changes, old files are simply discarded as parse errors
// (chat history is recoverable from nothing; data loss is acceptable
// here in a way it isn't for songs).
class JsonFileChatHistoryStore : public ChatHistoryStore {
public:
    explicit JsonFileChatHistoryStore(juce::File path);

    std::vector<ClaudeClient::Message> load() override;
    void save(const std::vector<ClaudeClient::Message>& messages) override;
    void clear() override;

private:
    juce::File file;

    static juce::var messageToVar(const ClaudeClient::Message& m);
    static ClaudeClient::Message varToMessage(const juce::var& v);
    static juce::var blockToVar(const ClaudeClient::ContentBlock& b);
    static ClaudeClient::ContentBlock varToBlock(const juce::var& v);
};

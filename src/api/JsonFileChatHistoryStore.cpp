#include "api/JsonFileChatHistoryStore.h"
#include "engine/Log.h"

namespace {
juce::String blockTypeToString(ClaudeClient::ContentBlock::Type t) {
    switch (t) {
        case ClaudeClient::ContentBlock::Text:       return "text";
        case ClaudeClient::ContentBlock::ToolUse:    return "tool_use";
        case ClaudeClient::ContentBlock::ToolResult: return "tool_result";
    }
    return "text";
}
ClaudeClient::ContentBlock::Type blockTypeFromString(const juce::String& s) {
    if (s == "tool_use")    return ClaudeClient::ContentBlock::ToolUse;
    if (s == "tool_result") return ClaudeClient::ContentBlock::ToolResult;
    return ClaudeClient::ContentBlock::Text;
}
}

JsonFileChatHistoryStore::JsonFileChatHistoryStore(juce::File path)
    : file(std::move(path)) {}

std::vector<ClaudeClient::Message> JsonFileChatHistoryStore::load() {
    if (! file.existsAsFile()) return {};

    auto parsed = juce::JSON::parse(file);
    if (! parsed.isObject()) {
        perfLog("[ChatHistory] load: file exists but not a JSON object — discarding\n");
        return {};
    }

    auto* messages = parsed.getProperty("messages", juce::var()).getArray();
    if (! messages) return {};

    std::vector<ClaudeClient::Message> out;
    out.reserve((size_t) messages->size());
    for (auto& m : *messages)
        out.push_back(varToMessage(m));
    return out;
}

void JsonFileChatHistoryStore::save(const std::vector<ClaudeClient::Message>& messages) {
    juce::Array<juce::var> arr;
    for (auto& m : messages) arr.add(messageToVar(m));

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("schema", 1);
    root->setProperty("messages", arr);
    auto json = juce::JSON::toString(juce::var(root.get()));

    // Atomic write: temp file + rename. Mid-save crash leaves the
    // previous file intact rather than truncating it.
    file.getParentDirectory().createDirectory();
    auto tmp = file.getSiblingFile(file.getFileNameWithoutExtension() + ".tmp");
    if (tmp.existsAsFile()) tmp.deleteFile();
    if (! tmp.replaceWithText(json)) {
        perfLog("[ChatHistory] save: failed to write temp file %s\n",
                tmp.getFullPathName().toRawUTF8());
        return;
    }
    if (! tmp.moveFileTo(file)) {
        perfLog("[ChatHistory] save: failed to rename temp to %s\n",
                file.getFullPathName().toRawUTF8());
    }
}

void JsonFileChatHistoryStore::clear() {
    if (file.existsAsFile()) file.deleteFile();
}

juce::var JsonFileChatHistoryStore::messageToVar(const ClaudeClient::Message& m) {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("role", m.role);
    juce::Array<juce::var> blocks;
    for (auto& b : m.content) blocks.add(blockToVar(b));
    obj->setProperty("content", blocks);
    return juce::var(obj.get());
}

ClaudeClient::Message JsonFileChatHistoryStore::varToMessage(const juce::var& v) {
    ClaudeClient::Message m;
    m.role = v.getProperty("role", "").toString();
    if (auto* blocks = v.getProperty("content", juce::var()).getArray()) {
        m.content.reserve((size_t) blocks->size());
        for (auto& b : *blocks) m.content.push_back(varToBlock(b));
    }
    return m;
}

juce::var JsonFileChatHistoryStore::blockToVar(const ClaudeClient::ContentBlock& b) {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("type", blockTypeToString(b.type));
    if (b.text.isNotEmpty())       obj->setProperty("text", b.text);
    if (b.toolUseId.isNotEmpty())  obj->setProperty("tool_use_id", b.toolUseId);
    if (b.toolName.isNotEmpty())   obj->setProperty("tool_name", b.toolName);
    if (b.toolInput.isNotEmpty())  obj->setProperty("tool_input", b.toolInput);
    if (b.toolOutput.isNotEmpty()) obj->setProperty("tool_output", b.toolOutput);
    if (b.isError)                  obj->setProperty("is_error", true);
    return juce::var(obj.get());
}

ClaudeClient::ContentBlock JsonFileChatHistoryStore::varToBlock(const juce::var& v) {
    ClaudeClient::ContentBlock b;
    b.type       = blockTypeFromString(v.getProperty("type", "text").toString());
    b.text       = v.getProperty("text", "").toString();
    b.toolUseId  = v.getProperty("tool_use_id", "").toString();
    b.toolName   = v.getProperty("tool_name", "").toString();
    b.toolInput  = v.getProperty("tool_input", "").toString();
    b.toolOutput = v.getProperty("tool_output", "").toString();
    b.isError    = (bool) v.getProperty("is_error", false);
    return b;
}

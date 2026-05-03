#include "api/ClaudeClient.h"
#include "scripting/LuaEngine.h"
#include "api/ChatHistoryStore.h"
#include "engine/Log.h"
#include "telemetry/InstallId.h"
#include "BuildConfig.h"

ClaudeClient::ClaudeClient(LuaEngine& lua)
    : Thread("ClaudeClient"), lua(lua) {}

ClaudeClient::~ClaudeClient() {
    stopThread(5000);
}

void ClaudeClient::setSystemPrompt(const juce::String& prompt) {
    systemPrompt = prompt;
}

void ClaudeClient::setHistoryStore(std::unique_ptr<ChatHistoryStore> store) {
    historyStore = std::move(store);
    if (! historyStore) return;
    auto loaded = historyStore->load();
    if (loaded.empty()) return;
    conversationHistory = std::move(loaded);
    perfLog("[ChatHistory] loaded %d message(s) from store\n",
            (int) conversationHistory.size());
}

void ClaudeClient::clearHistory() {
    conversationHistory.clear();
    if (historyStore) historyStore->clear();
    if (listener) listener->onHistoryCleared();
    perfLog("[ChatHistory] cleared\n");
}

void ClaudeClient::trimHistoryForContext() {
    if (conversationHistory.size() <= kMaxContextMessages) return;

    // Drop oldest first, but keep turn integrity. The Anthropic API
    // requires every tool_use block to have a matching tool_result block
    // somewhere later in the conversation, and we can't have a stray
    // tool_result without its tool_use either. So: walk forward from the
    // oldest message, dropping until we're under the cap AND the next-to-
    // drop message would leave a tool_use orphaned.
    //
    // Simplest correct rule: only drop a message if it (a) brings us
    // closer to the cap and (b) doesn't contain tool_use blocks whose
    // tool_result still lives in the kept tail. Since tool_use is always
    // in an assistant message and its tool_result is in the immediately
    // following user message, drop in pairs (assistant + the user turn
    // that follows it, OR a user turn that has no tool-related blocks).
    while (conversationHistory.size() > kMaxContextMessages) {
        // Front-most message — safe to drop a plain user text message
        // alone, OR an assistant message together with the user message
        // that follows it (which carries the tool_results).
        auto& front = conversationHistory.front();
        bool frontHasToolUse = false;
        for (auto& b : front.content)
            if (b.type == ContentBlock::ToolUse) { frontHasToolUse = true; break; }

        if (frontHasToolUse && conversationHistory.size() >= 2) {
            conversationHistory.erase(conversationHistory.begin(),
                                       conversationHistory.begin() + 2);
        } else {
            conversationHistory.erase(conversationHistory.begin());
        }
    }
}

void ClaudeClient::persistHistory() {
    if (! historyStore) return;
    historyStore->save(conversationHistory);
}

void ClaudeClient::sendMessage(const juce::String& userText) {
    if (isThreadRunning()) return;

    Message userMsg;
    userMsg.role = "user";
    userMsg.content.push_back({ ContentBlock::Text, userText, {}, {}, {}, {}, false });
    conversationHistory.push_back(userMsg);
    perfLog("[Chat] User: %s\n", userText.toRawUTF8());

    // Trim before sending so the request stays under the context cap;
    // persist immediately so a crash before the assistant replies still
    // captures the user's message.
    trimHistoryForContext();
    persistHistory();

    startThread();
}

void ClaudeClient::run() {
    notifyBusy(true);

    if (juce::String(CHAT_PROXY_URL).isEmpty()) {
        notifyError("Chat proxy not configured (run scripts/fetch-telemetry-config.sh and rebuild)");
        notifyBusy(false);
        return;
    }

    while (!threadShouldExit()) {
        auto requestJson = buildRequestJson();

        juce::URL url(CHAT_PROXY_URL);
        url = url.withPOSTData(requestJson);

        juce::String headers;
        headers += "Authorization: Bearer " + juce::String(TELEMETRY_TOKEN) + "\r\n";
        headers += "X-Install-Id: " + InstallId::id() + "\r\n";
        headers += "content-type: application/json";

        int statusCode = 0;
        auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
            .withExtraHeaders(headers)
            .withConnectionTimeoutMs(60000)
            .withStatusCode(&statusCode);

        auto stream = url.createInputStream(options);

        if (!stream) {
            notifyError("Failed to connect to chat proxy");
            break;
        }

        if (statusCode == 401) {
            notifyError("Chat auth failed — rebuild needed (token rotated?)");
            break;
        }
        if (statusCode == 402) {
            // Body has the human-readable message; surface it.
            auto body = stream->readEntireStreamAsString();
            auto parsed = juce::JSON::parse(body);
            auto msg = parsed.getProperty("error", "Free chat budget exhausted").toString();
            notifyError(msg);
            break;
        }
        if (statusCode == 429) {
            notifyError("Rate limited — try again in a moment");
            break;
        }
        if (statusCode != 200) {
            auto body = stream->readEntireStreamAsString();
            auto parsed = juce::JSON::parse(body);
            auto errorMsg = parsed.getProperty("error", body).toString();
            perfLog("[Claude] HTTP %d: %s\n", statusCode, body.toRawUTF8());
            notifyError("API error (" + juce::String(statusCode) + "): " + errorMsg);
            break;
        }

        Message assistantMsg;
        if (!streamResponse(*stream, assistantMsg)) break;

        conversationHistory.push_back(assistantMsg);

        // Log every text block as soon as the assistant turn arrives —
        // text emitted alongside a tool call would otherwise never surface
        // to the log (notifyText only fires on text-only turns).
        for (auto& block : assistantMsg.content) {
            if (block.type == ContentBlock::Text && block.text.isNotEmpty())
                perfLog("[Chat] Assistant: %s\n", block.text.toRawUTF8());
        }

        // Tool-use loop: if the assistant called any tools, run them and continue.
        bool hasToolUse = false;
        Message toolResultMsg;
        toolResultMsg.role = "user";

        for (auto& block : assistantMsg.content) {
            if (block.type != ContentBlock::ToolUse) continue;
            hasToolUse = true;

            auto inputJson = juce::JSON::parse(block.toolInput);
            auto code = inputJson.getProperty("code", "").toString();

            if (code.isEmpty()) {
                perfLog("[Claude] WARNING: Empty code from tool input: %s\n",
                        block.toolInput.toRawUTF8());
            } else {
                perfLog("[Claude] Tool call (%d chars): %s\n",
                        code.length(), code.toRawUTF8());
            }

            auto result = executeLua(code);
            perfLog("[Claude] Tool result: %s\n", result.toRawUTF8());

            bool isErr = result.startsWith("error:");
            notifyToolUse(block.toolName, code, result, isErr);

            ContentBlock resultBlock;
            resultBlock.type = ContentBlock::ToolResult;
            resultBlock.toolUseId = block.toolUseId;
            resultBlock.toolOutput = result;
            resultBlock.isError = isErr;
            toolResultMsg.content.push_back(resultBlock);
        }

        if (!hasToolUse) {
            juce::String fullText;
            for (auto& block : assistantMsg.content)
                if (block.type == ContentBlock::Text)
                    fullText += block.text;

            if (fullText.isNotEmpty())
                notifyText(fullText);
            break;
        }

        conversationHistory.push_back(toolResultMsg);
    }

    // Round-trip complete (loop ended cleanly OR via early-out break).
    // Persist the final conversation so a relaunch picks up where we
    // left off. Trim again in case the round-trip pushed past the cap.
    trimHistoryForContext();
    persistHistory();

    notifyBusy(false);
}

bool ClaudeClient::streamResponse(juce::InputStream& stream, Message& msg) {
    msg.role = "assistant";

    // SSE parser. Anthropic emits events of the form:
    //   event: <type>\n
    //   data: <json>\n
    //   \n
    // Multiple data lines per event are possible; we concatenate them.
    std::string buffer;
    juce::String currentEvent;
    juce::String currentData;

    // Per-block accumulators. Keyed by content block index.
    struct BlockState {
        ContentBlock block;
        juce::String inputJsonAccum;  // for tool_use input_json_delta
    };
    std::map<int, BlockState> blocks;

    auto handleEvent = [&](const juce::String& evt, const juce::String& data) {
        if (data.isEmpty() || data == "[DONE]") return;
        auto parsed = juce::JSON::parse(data);

        if (evt == "content_block_start") {
            int idx = (int)parsed.getProperty("index", 0);
            auto cb = parsed.getProperty("content_block", juce::var());
            auto type = cb.getProperty("type", "").toString();
            BlockState bs;
            if (type == "text") {
                bs.block.type = ContentBlock::Text;
            } else if (type == "tool_use") {
                bs.block.type = ContentBlock::ToolUse;
                bs.block.toolUseId = cb.getProperty("id", "").toString();
                bs.block.toolName = cb.getProperty("name", "").toString();
            }
            blocks[idx] = std::move(bs);
        }
        else if (evt == "content_block_delta") {
            int idx = (int)parsed.getProperty("index", 0);
            auto delta = parsed.getProperty("delta", juce::var());
            auto deltaType = delta.getProperty("type", "").toString();
            auto it = blocks.find(idx);
            if (it == blocks.end()) return;

            if (deltaType == "text_delta") {
                it->second.block.text += delta.getProperty("text", "").toString();
            } else if (deltaType == "input_json_delta") {
                it->second.inputJsonAccum += delta.getProperty("partial_json", "").toString();
            }
        }
        else if (evt == "content_block_stop") {
            int idx = (int)parsed.getProperty("index", 0);
            auto it = blocks.find(idx);
            if (it == blocks.end()) return;
            if (it->second.block.type == ContentBlock::ToolUse)
                it->second.block.toolInput = it->second.inputJsonAccum;
        }
        // message_start / message_delta / message_stop / ping: ignored — we
        // collect all blocks via content_block_* events and finalize below.
    };

    constexpr int chunkSize = 4096;
    juce::HeapBlock<char> chunk(chunkSize);

    while (!threadShouldExit() && !stream.isExhausted()) {
        int read = stream.read(chunk.getData(), chunkSize);
        if (read <= 0) break;
        buffer.append(chunk.getData(), (size_t)read);

        size_t nl;
        while ((nl = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, nl);
            buffer.erase(0, nl + 1);
            // Strip trailing \r if present (some servers send CRLF)
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.empty()) {
                // Blank line = end of an SSE event
                if (currentData.isNotEmpty())
                    handleEvent(currentEvent, currentData);
                currentEvent = "";
                currentData = "";
                continue;
            }
            if (line.rfind("event: ", 0) == 0) {
                currentEvent = juce::String(line.substr(7));
            } else if (line.rfind("data: ", 0) == 0) {
                if (currentData.isNotEmpty()) currentData += "\n";
                currentData += juce::String(line.substr(6));
            }
            // Other SSE fields (id:, retry:) are ignored.
        }
    }

    // Finalize blocks in index order
    for (auto& [idx, bs] : blocks)
        msg.content.push_back(std::move(bs.block));

    return true;
}

juce::String ClaudeClient::buildRequestJson() {
    auto* body = new juce::DynamicObject();
    // model is supplied by the proxy from DEFAULT_MODEL env var; we don't pin it here.
    body->setProperty("max_tokens", 8192);
    body->setProperty("stream", true);

    // Send the system prompt as a structured block with cache_control so
    // Anthropic caches it for 5 minutes. After the first request in that
    // window, the ~13k-token system prompt is read from cache at 10% of
    // the normal input price (and counts at the same weighting against
    // our Lambda's per-install cap). Tools come before system in the
    // cache prefix, so marking the system block caches tools too.
    if (systemPrompt.isNotEmpty()) {
        juce::Array<juce::var> systemArr;
        auto* sysBlock = new juce::DynamicObject();
        sysBlock->setProperty("type", "text");
        sysBlock->setProperty("text", systemPrompt);
        auto* cacheCtl = new juce::DynamicObject();
        cacheCtl->setProperty("type", "ephemeral");
        sysBlock->setProperty("cache_control", juce::var(cacheCtl));
        systemArr.add(juce::var(sysBlock));
        body->setProperty("system", systemArr);
    }

    juce::Array<juce::var> messagesArr;
    for (auto& msg : conversationHistory) {
        auto* msgObj = new juce::DynamicObject();
        msgObj->setProperty("role", msg.role);

        juce::Array<juce::var> contentArr;
        for (auto& block : msg.content) {
            auto* blockObj = new juce::DynamicObject();
            if (block.type == ContentBlock::Text) {
                blockObj->setProperty("type", "text");
                blockObj->setProperty("text", block.text);
            } else if (block.type == ContentBlock::ToolUse) {
                blockObj->setProperty("type", "tool_use");
                blockObj->setProperty("id", block.toolUseId);
                blockObj->setProperty("name", block.toolName);
                blockObj->setProperty("input", juce::JSON::parse(block.toolInput));
            } else if (block.type == ContentBlock::ToolResult) {
                blockObj->setProperty("type", "tool_result");
                blockObj->setProperty("tool_use_id", block.toolUseId);
                blockObj->setProperty("content", block.toolOutput);
                if (block.isError)
                    blockObj->setProperty("is_error", true);
            }
            contentArr.add(juce::var(blockObj));
        }
        msgObj->setProperty("content", contentArr);
        messagesArr.add(juce::var(msgObj));
    }
    body->setProperty("messages", messagesArr);

    juce::Array<juce::var> toolsArr;
    {
        auto* tool = new juce::DynamicObject();
        tool->setProperty("name", "perf");
        tool->setProperty("description",
            "Execute Lua code in the running performance engine. Returns the result as a string. "
            "Use this to create tracks, add instruments, set gains, etc.");

        auto* schema = new juce::DynamicObject();
        schema->setProperty("type", "object");

        auto* props = new juce::DynamicObject();
        auto* codeProp = new juce::DynamicObject();
        codeProp->setProperty("type", "string");
        codeProp->setProperty("description", "Lua code to execute in the engine");
        props->setProperty("code", juce::var(codeProp));
        schema->setProperty("properties", juce::var(props));

        juce::Array<juce::var> required;
        required.add("code");
        schema->setProperty("required", required);

        tool->setProperty("input_schema", juce::var(schema));
        toolsArr.add(juce::var(tool));
    }
    body->setProperty("tools", toolsArr);

    return juce::JSON::toString(juce::var(body), true);
}

juce::String ClaudeClient::executeLua(const juce::String& code) {
    struct Params {
        LuaEngine* lua;
        juce::String code;
        std::string result;
    };
    Params p { &lua, code, {} };

    juce::MessageManager::getInstance()->callFunctionOnMessageThread(
        [](void* data) -> void* {
            auto* p = static_cast<Params*>(data);
            p->result = p->lua->executeString(p->code.toStdString());
            return nullptr;
        }, &p);

    return juce::String(p.result);
}

void ClaudeClient::notifyText(const juce::String& text) {
    juce::MessageManager::callAsync([this, text] {
        if (listener) listener->onAssistantText(text);
    });
}

void ClaudeClient::notifyToolUse(const juce::String& name, const juce::String& code,
                                  const juce::String& result, bool isError) {
    juce::MessageManager::callAsync([this, name, code, result, isError] {
        if (listener) listener->onToolUse(name, code, result, isError);
    });
}

void ClaudeClient::notifyError(const juce::String& error) {
    juce::MessageManager::callAsync([this, error] {
        if (listener) listener->onError(error);
    });
}

void ClaudeClient::notifyBusy(bool busy) {
    juce::MessageManager::callAsync([this, busy] {
        if (listener) listener->onBusyChanged(busy);
    });
}

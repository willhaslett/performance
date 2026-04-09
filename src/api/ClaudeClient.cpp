#include "api/ClaudeClient.h"
#include "scripting/LuaEngine.h"
#include "engine/Log.h"

ClaudeClient::ClaudeClient(LuaEngine& lua)
    : Thread("ClaudeClient"), lua(lua) {
    auto* key = getenv("ANTHROPIC_API_KEY");
    if (key) apiKey = juce::String(key);
}

ClaudeClient::~ClaudeClient() {
    stopThread(5000);
}

void ClaudeClient::setSystemPrompt(const juce::String& prompt) {
    systemPrompt = prompt;
}

void ClaudeClient::sendMessage(const juce::String& userText) {
    if (isThreadRunning()) return;

    // Add user message to history
    Message userMsg;
    userMsg.role = "user";
    userMsg.content.push_back({ ContentBlock::Text, userText, {}, {}, {}, {}, false });
    conversationHistory.push_back(userMsg);

    startThread();
}

void ClaudeClient::run() {
    notifyBusy(true);

    if (apiKey.isEmpty()) {
        notifyError("Set ANTHROPIC_API_KEY environment variable to use Claude");
        notifyBusy(false);
        return;
    }

    // Agentic loop: call API, handle tool use, repeat
    while (!threadShouldExit()) {
        auto requestJson = buildRequestJson();

        // Make HTTP request
        juce::URL url("https://api.anthropic.com/v1/messages");
        url = url.withPOSTData(requestJson);

        juce::String headers;
        headers += "x-api-key: " + apiKey + "\r\n";
        headers += "anthropic-version: 2023-06-01\r\n";
        headers += "content-type: application/json";

        int statusCode = 0;
        auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
            .withExtraHeaders(headers)
            .withConnectionTimeoutMs(60000)
            .withStatusCode(&statusCode);

        auto stream = url.createInputStream(options);

        if (!stream) {
            notifyError("Failed to connect to Claude API");
            break;
        }

        auto responseBody = stream->readEntireStreamAsString();

        if (statusCode == 429) {
            notifyError("Rate limited — try again in a moment");
            break;
        }

        if (statusCode != 200) {
            // Try to extract error message
            auto parsed = juce::JSON::parse(responseBody);
            auto errorMsg = parsed.getProperty("error", juce::var())
                                  .getProperty("message", responseBody).toString();
            notifyError("API error (" + juce::String(statusCode) + "): " + errorMsg);
            break;
        }

        // Parse response
        auto assistantMsg = parseResponse(responseBody);
        conversationHistory.push_back(assistantMsg);

        // Check for tool use
        bool hasToolUse = false;
        Message toolResultMsg;
        toolResultMsg.role = "user";

        for (auto& block : assistantMsg.content) {
            if (block.type == ContentBlock::ToolUse) {
                hasToolUse = true;

                // Extract code from input JSON
                auto inputJson = juce::JSON::parse(block.toolInput);
                auto code = inputJson.getProperty("code", "").toString();

                if (code.isEmpty()) {
                    perfLog("[Claude] WARNING: Empty code from tool input: %s\n",
                            block.toolInput.toRawUTF8());
                } else {
                    perfLog("[Claude] Tool call (%d chars): %s\n",
                            code.length(), code.toRawUTF8());
                }

                // Execute on message thread
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
        }

        if (!hasToolUse) {
            // Collect all text blocks and send to UI
            juce::String fullText;
            for (auto& block : assistantMsg.content)
                if (block.type == ContentBlock::Text)
                    fullText += block.text;

            if (fullText.isNotEmpty())
                notifyText(fullText);
            break;
        }

        // Append tool results and loop
        conversationHistory.push_back(toolResultMsg);
    }

    notifyBusy(false);
}

juce::String ClaudeClient::buildRequestJson() {
    auto* body = new juce::DynamicObject();
    body->setProperty("model", model);
    body->setProperty("max_tokens", 8192);

    if (systemPrompt.isNotEmpty())
        body->setProperty("system", systemPrompt);

    // Messages
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

    // Tool definition
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

ClaudeClient::Message ClaudeClient::parseResponse(const juce::String& responseBody) {
    Message msg;
    msg.role = "assistant";

    auto parsed = juce::JSON::parse(responseBody);
    auto content = parsed.getProperty("content", juce::var());

    if (auto* arr = content.getArray()) {
        for (auto& item : *arr) {
            auto type = item.getProperty("type", "").toString();
            ContentBlock block;

            if (type == "text") {
                block.type = ContentBlock::Text;
                block.text = item.getProperty("text", "").toString();
            } else if (type == "tool_use") {
                block.type = ContentBlock::ToolUse;
                block.toolUseId = item.getProperty("id", "").toString();
                block.toolName = item.getProperty("name", "").toString();
                block.toolInput = juce::JSON::toString(item.getProperty("input", juce::var()));
            }

            msg.content.push_back(block);
        }
    }

    return msg;
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

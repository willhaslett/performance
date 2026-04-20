#pragma once

#include "composer/ComposerOutput.h"
#include <juce_core/juce_core.h>
#include <memory>
#include <string>

// A NotationParser turns a string of LLM-emitted notation into
// format-independent ComposerOutput. Multiple implementations can
// coexist (one at a time is active); we start with dawai's v2
// notation and add others as we find better fits.
//
// Parsers are pure — no I/O, no global state — so they're trivially
// testable with fixture strings.
class NotationParser {
public:
    virtual ~NotationParser() = default;

    // Parse `input`. On success, fills `out` and returns true.
    // On failure, fills `err` with a human-readable message and
    // returns false; `out` may be partially populated (callers
    // must ignore it on failure).
    virtual bool parse(const juce::String& input,
                       ComposerOutput& out,
                       std::string& err) = 0;
};

// Factory — returns whatever parser is currently considered the
// default. Centralizes the one place format selection happens so
// a future runtime-switch (or build-time override) has a single
// touchpoint.
std::unique_ptr<NotationParser> makeDefaultNotationParser();

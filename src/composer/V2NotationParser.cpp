#include "composer/V2NotationParser.h"

bool V2NotationParser::parse(const juce::String& /*input*/,
                              ComposerOutput& /*out*/,
                              std::string& err) {
    // Phase 1 stub — actual grammar lands in Phase 2. The scaffold
    // exists so callers (the future Lua `compose` binding, the
    // ComposerWriter, and unit tests) can wire up against a stable
    // interface before the grammar is implemented.
    err = "V2NotationParser: parse() not implemented yet (Phase 2)";
    return false;
}

#include "composer/NotationParser.h"
#include "composer/V2NotationParser.h"

std::unique_ptr<NotationParser> makeDefaultNotationParser() {
    return std::make_unique<V2NotationParser>();
}

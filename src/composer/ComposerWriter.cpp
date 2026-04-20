#include "composer/ComposerWriter.h"
#include "api/StateAPI.h"

bool ComposerWriter::apply(const ComposerOutput& /*output*/,
                            double /*startBeat*/,
                            std::string& err) {
    // Phase 1 stub — real implementation lands in Phase 2 once the
    // V2 parser produces real output. The signature is stable so
    // the Lua `compose` binding (Phase 3) can be written against it.
    err = "ComposerWriter: apply() not implemented yet (Phase 2)";
    return false;
}

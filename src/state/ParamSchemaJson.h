#pragma once
#include "state/StateModel.h"
#include <string>
#include <vector>

// Serialize / deserialize typed ParamSchema to/from JSON. The grammar is
// documented in docs/ACTION_INSTANCES_REFACTOR.md. Emits only non-default
// fields (compact) and tolerates missing fields on parse (forward-compat
// for user-defined schemas).
namespace ParamSchemaJson {

std::string toJson(const std::vector<ParamSchema>& params);
std::vector<ParamSchema> fromJson(const std::string& json);

// Type name round-trip (for readers that want to inspect without a full parse).
std::string typeToString(ParamType t);
ParamType   typeFromString(const std::string& s);

}  // namespace ParamSchemaJson

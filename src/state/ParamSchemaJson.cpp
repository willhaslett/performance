#include "state/ParamSchemaJson.h"
#include <juce_core/juce_core.h>

namespace ParamSchemaJson {

std::string typeToString(ParamType t) {
    switch (t) {
        case ParamType::ChannelRef: return "channelRef";
        case ParamType::PresetRef:  return "presetRef";
        case ParamType::Enum:       return "enum";
        case ParamType::Float:      return "float";
        case ParamType::Morph:      return "morph";
    }
    return "float";
}

ParamType typeFromString(const std::string& s) {
    if (s == "channelRef") return ParamType::ChannelRef;
    if (s == "presetRef")  return ParamType::PresetRef;
    if (s == "enum")       return ParamType::Enum;
    if (s == "morph")      return ParamType::Morph;
    return ParamType::Float;
}

std::string toJson(const std::vector<ParamSchema>& params) {
    juce::var arr;
    for (auto& p : params) {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("name", juce::String(p.name));
        obj->setProperty("type", juce::String(typeToString(p.type)));
        // Only emit fields that differ from defaults, to keep storage compact.
        if (!p.required) obj->setProperty("required", false);
        if (!p.defaultValue.empty()) obj->setProperty("default", juce::String(p.defaultValue));
        if (!p.scope.empty()) {
            juce::var scopeArr;
            for (auto& s : p.scope) scopeArr.append(juce::String(s));
            obj->setProperty("scope", scopeArr);
        }
        if (!p.sourceTypes.empty()) {
            juce::var stArr;
            for (auto& s : p.sourceTypes) stArr.append(juce::String(s));
            obj->setProperty("sourceTypes", stArr);
        }
        if (!p.enumValues.empty()) {
            juce::var evArr;
            for (auto& s : p.enumValues) evArr.append(juce::String(s));
            obj->setProperty("enumValues", evArr);
        }
        if (p.minValue) obj->setProperty("min", *p.minValue);
        if (p.maxValue) obj->setProperty("max", *p.maxValue);
        arr.append(juce::var(obj));
    }
    return juce::JSON::toString(arr, true).toStdString();
}

static std::vector<std::string> readStringArray(const juce::var& v) {
    std::vector<std::string> out;
    if (auto* arr = v.getArray())
        for (auto& item : *arr) out.push_back(item.toString().toStdString());
    return out;
}

std::vector<ParamSchema> fromJson(const std::string& json) {
    std::vector<ParamSchema> out;
    auto parsed = juce::JSON::parse(juce::String(json));
    auto* arr = parsed.getArray();
    if (!arr) return out;

    for (auto& entry : *arr) {
        ParamSchema p;
        p.name = entry.getProperty("name", "").toString().toStdString();
        p.type = typeFromString(entry.getProperty("type", "float").toString().toStdString());
        p.required = (bool)entry.getProperty("required", true);
        p.defaultValue = entry.getProperty("default", "").toString().toStdString();
        p.scope       = readStringArray(entry.getProperty("scope", juce::var()));
        p.sourceTypes = readStringArray(entry.getProperty("sourceTypes", juce::var()));
        p.enumValues  = readStringArray(entry.getProperty("enumValues", juce::var()));
        auto mn = entry.getProperty("min", juce::var());
        auto mx = entry.getProperty("max", juce::var());
        if (!mn.isVoid() && !mn.isUndefined()) p.minValue = (double)mn;
        if (!mx.isVoid() && !mx.isUndefined()) p.maxValue = (double)mx;
        out.push_back(std::move(p));
    }
    return out;
}

}  // namespace ParamSchemaJson

#include "telemetry/InstallId.h"
#include "engine/Log.h"

namespace {

juce::File installFile() {
    // juce::File::userApplicationDataDirectory on macOS is ~/Library, not
    // ~/Library/Application Support — JUCE expects you to append the
    // conventional subpath.
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("Application Support")
               .getChildFile("com.performance.app")
               .getChildFile("install.json");
}

struct Identity {
    juce::String id;
    juce::String firstSeen;
};

const Identity& identity() {
    static Identity cached = [] {
        Identity out;
        auto file = installFile();

        if (file.existsAsFile()) {
            auto json = juce::JSON::parse(file);
            if (auto* obj = json.getDynamicObject()) {
                out.id = obj->getProperty("id").toString();
                out.firstSeen = obj->getProperty("firstSeen").toString();
                if (out.id.isNotEmpty())
                    return out;
            }
            perfLog("[InstallId] install.json present but unreadable — regenerating\n");
        }

        file.getParentDirectory().createDirectory();
        out.id = juce::Uuid().toDashedString();
        out.firstSeen = juce::Time::getCurrentTime().toISO8601(true);

        auto* obj = new juce::DynamicObject();
        obj->setProperty("id", out.id);
        obj->setProperty("firstSeen", out.firstSeen);
        file.replaceWithText(juce::JSON::toString(juce::var(obj)));
        perfLog("[InstallId] Generated new install ID: %s\n", out.id.toRawUTF8());
        return out;
    }();
    return cached;
}

}  // namespace

namespace InstallId {
juce::String id()        { return identity().id; }
juce::String firstSeen() { return identity().firstSeen; }
}

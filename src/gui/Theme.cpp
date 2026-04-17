#include "Theme.h"
#include "BinaryData.h"
#include <juce_core/juce_core.h>
#include <unordered_map>
#include <string>
#include <optional>

namespace Theme {

namespace {
    juce::String currentTheme = "Minimal Dark";

    // --- Factory theme registry -----------------------------------------------
    struct FactoryTheme {
        const char* id;
        const char* binaryResourceName;  // matches BinaryData key (e.g. "minimal_dark_json")
    };

    const FactoryTheme kFactoryThemes[] = {
        { "minimal_dark",  "minimal_dark_json"  },
        { "minimal_light", "minimal_light_json" },
    };
    constexpr int kFactoryThemeCount = 2;

    using ColorTable = std::unordered_map<std::string, uint32_t*>;
    using FloatTable = std::unordered_map<std::string, float*>;
    using IntTable   = std::unordered_map<std::string, int*>;

    ColorTable& colorTable() {
        static ColorTable t = {
            {"bgApp",           &Color::bgApp},
            {"bgPanel",         &Color::bgPanel},
            {"bgSurface",       &Color::bgSurface},
            {"bgSurfaceRaised", &Color::bgSurfaceRaised},
            {"bgSlot",          &Color::bgSlot},
            {"bgControl",       &Color::bgControl},
            {"bgControlHover",  &Color::bgControlHover},
            {"bgListActive",    &Color::bgListActive},
            {"bgSelection",     &Color::bgSelection},
            {"bgStripe",        &Color::bgStripe},
            {"bgOverlay",       &Color::bgOverlay},
            {"bgDisabled",      &Color::bgDisabled},
            {"bgRecessed",      &Color::bgRecessed},
            {"overlayDim",      &Color::overlayDim},
            {"border",          &Color::border},
            {"borderSubtle",    &Color::borderSubtle},
            {"textPrimary",     &Color::textPrimary},
            {"textSecondary",   &Color::textSecondary},
            {"textDim",         &Color::textDim},
            {"textKeyHint",     &Color::textKeyHint},
            {"textOnColor",     &Color::textOnColor},
            {"controlHandle",   &Color::controlHandle},
            {"accent",          &Color::accent},
            {"accentDim",       &Color::accentDim},
            {"activityOn",      &Color::activityOn},
            {"activityOff",     &Color::activityOff},
            {"statusError",     &Color::statusError},
            {"transportPlay",     &Color::transportPlay},
            {"transportRec",      &Color::transportRec},
            {"transportRecDot",   &Color::transportRecDot},
            {"transportCycle",    &Color::transportCycle},
            {"transportCycleOff", &Color::transportCycleOff},
            {"playhead",        &Color::playhead},
            {"meterGreen",      &Color::meterGreen},
            {"meterAmber",      &Color::meterAmber},
            {"meterRed",        &Color::meterRed},
            {"sendPeak",        &Color::sendPeak},
            {"typeInstrument",  &Color::typeInstrument},
            {"typeAudio",       &Color::typeAudio},
            {"typeBus",         &Color::typeBus},
            {"typeOutput",      &Color::typeOutput},
            {"pillMute",        &Color::pillMute},
            {"pillSolo",        &Color::pillSolo},
            {"pillArm",         &Color::pillArm},
            {"pillInput",       &Color::pillInput},
            {"pillTextOff",     &Color::pillTextOff},
            {"slotInstrument",  &Color::slotInstrument},
            {"slotEffect",      &Color::slotEffect},
            {"lcdDigit",        &Color::lcdDigit},
            {"chatUser",        &Color::chatUser},
            {"chatAssistant",   &Color::chatAssistant},
            {"chatTool",        &Color::chatTool},
            {"chatError",       &Color::chatError},
            {"chatInput",       &Color::chatInput},
        };
        return t;
    }

    FloatTable& floatTable() {
        static FloatTable t = {
            // fonts
            {"Title",    &fontSizeTitle},
            {"KeyHint",  &fontSizeKeyHint},
            {"Lg",       &fontSizeLg},
            {"Md",       &fontSizeMd},
            {"Sm",       &fontSizeSm},
            {"Xs",       &fontSizeXs},
            {"Pill",     &fontSizePill},
            {"Meter",    &fontSizeMeter},
            {"LcdLg",    &fontSizeLcdLg},
            {"LcdMd",    &fontSizeLcdMd},
            {"LcdLabel", &fontSizeLcdLabel},
            // radii
            {"cornerRadius",     &cornerRadius},
            {"cornerRadiusSm",   &cornerRadiusSm},
            {"cornerRadiusXs",   &cornerRadiusXs},
            {"pillRadius",       &pillRadius},
            {"activityDotSize",  &activityDotSize},
            {"chatBubbleRadius", &chatBubbleRadius},
        };
        return t;
    }

    IntTable& intTable() {
        static IntTable t = {
            // spacing
            {"Xs", &spacingXs},
            {"S",  &spacingS},
            {"M",  &spacingM},
            {"L",  &spacingL},
            {"Xl", &spacingXl},
            // dimensions
            {"headerHeight",    &headerHeight},
            {"toolbarHeight",   &toolbarHeight},
            {"slotHeight",      &slotHeight},
            {"slotGap",         &slotGap},
            {"slotPadding",     &slotPadding},
            {"trackPadding",    &trackPadding},
            {"trackStripWidth", &trackStripWidth},
            {"iconSize",        &iconSize},
            {"pillSize",        &pillSize},
            {"pillGap",         &pillGap},
            {"pillGroupGap",    &pillGroupGap},
            {"pillNameGap",     &pillNameGap},
            {"chatBubblePad",   &chatBubblePad},
            {"chatGap",         &chatGap},
            {"chatInputHeight", &chatInputHeight},
        };
        return t;
    }

    std::optional<uint32_t> parseHexColor(const juce::String& s) {
        juce::String str = s.trim();
        if (! str.startsWithChar('#')) return std::nullopt;
        str = str.substring(1);
        if (str.length() != 6 && str.length() != 8) return std::nullopt;
        for (int i = 0; i < str.length(); ++i) {
            auto c = str[i];
            bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (! hex) return std::nullopt;
        }
        auto val = (uint32_t) str.getHexValue64();
        if (str.length() == 6) val |= 0xff000000u;
        return val;
    }

    juce::String hexFromColor(uint32_t c) {
        auto hex = juce::String::toHexString((juce::int64) c).toLowerCase();
        while (hex.length() < 8) hex = "0" + hex;
        return "#" + hex;
    }

    void applyColorsObject(const juce::var& colorsVar) {
        if (! colorsVar.isObject()) return;
        auto* obj = colorsVar.getDynamicObject();
        if (obj == nullptr) return;
        auto& table = colorTable();
        for (auto& prop : obj->getProperties()) {
            std::string key = prop.name.toString().toStdString();
            auto it = table.find(key);
            if (it == table.end()) {
                juce::Logger::writeToLog("[Theme] Unknown color key: " + juce::String(key));
                continue;
            }
            auto parsed = parseHexColor(prop.value.toString());
            if (! parsed) {
                juce::Logger::writeToLog("[Theme] Invalid color for " + juce::String(key) + ": " + prop.value.toString());
                continue;
            }
            *(it->second) = *parsed;
        }
    }

    void applyColorArray(const juce::var& arrVar, std::array<uint32_t, 8>& target) {
        if (! arrVar.isArray()) return;
        auto* arr = arrVar.getArray();
        if (arr == nullptr) return;
        int n = juce::jmin((int) target.size(), arr->size());
        for (int i = 0; i < n; ++i) {
            auto parsed = parseHexColor((*arr)[i].toString());
            if (parsed) target[(size_t) i] = *parsed;
        }
    }

    template <typename T, typename Table>
    void applyScalarObject(const juce::var& sectionVar, Table& table) {
        if (! sectionVar.isObject()) return;
        auto* obj = sectionVar.getDynamicObject();
        if (obj == nullptr) return;
        for (auto& prop : obj->getProperties()) {
            std::string key = prop.name.toString().toStdString();
            auto it = table.find(key);
            if (it == table.end()) {
                juce::Logger::writeToLog("[Theme] Unknown key: " + juce::String(key));
                continue;
            }
            *(it->second) = (T) (double) prop.value;
        }
    }
} // namespace

// Apply a parsed JSON theme object to the in-memory token values.
// Called by both loadTheme(File) and loadThemeById().
static bool loadThemeFromVar(const juce::var& parsed) {
    if (! parsed.isObject()) return false;

    if (parsed.hasProperty("name"))
        currentTheme = parsed["name"].toString();

    applyColorsObject(parsed["colors"]);
    applyColorArray(parsed["trackColors"], Color::trackColors);
    applyColorArray(parsed["deviceColors"], Color::deviceColors);

    applyScalarObject<float>(parsed["fonts"],      floatTable());
    applyScalarObject<int>  (parsed["spacing"],    intTable());
    applyScalarObject<int>  (parsed["dimensions"], intTable());
    applyScalarObject<float>(parsed["radii"],      floatTable());
    applyScalarObject<float>(parsed["dimensions"], floatTable());
    applyScalarObject<int>  (parsed["radii"],      intTable());

    return true;
}

bool loadTheme(const juce::File& file) {
    if (! file.existsAsFile()) {
        juce::Logger::writeToLog("[Theme] Theme file not found: " + file.getFullPathName());
        return false;
    }
    auto parsed = juce::JSON::parse(file);
    if (! loadThemeFromVar(parsed)) {
        juce::Logger::writeToLog("[Theme] Failed to parse JSON: " + file.getFullPathName());
        return false;
    }
    juce::Logger::writeToLog("[Theme] Loaded theme '" + currentTheme + "' from " + file.getFullPathName());
    return true;
}

bool saveTheme(const juce::File& file, const juce::String& name, const juce::String& description) {
    auto* root = new juce::DynamicObject();
    root->setProperty("name", name);
    root->setProperty("description", description);
    root->setProperty("version", 1);

    // colors
    auto* colors = new juce::DynamicObject();
    for (auto& kv : colorTable())
        colors->setProperty(juce::String(kv.first), hexFromColor(*kv.second));
    root->setProperty("colors", juce::var(colors));

    // trackColors / deviceColors arrays
    juce::Array<juce::var> tc;
    for (auto v : Color::trackColors) tc.add(hexFromColor(v));
    root->setProperty("trackColors", tc);

    juce::Array<juce::var> dc;
    for (auto v : Color::deviceColors) dc.add(hexFromColor(v));
    root->setProperty("deviceColors", dc);

    // fonts
    auto* fonts = new juce::DynamicObject();
    const char* fontKeys[] = {"Title","Lg","Md","Sm","Xs","Pill","Meter","LcdLg","LcdMd","LcdLabel"};
    auto& ft = floatTable();
    for (auto* k : fontKeys) {
        auto it = ft.find(k);
        if (it != ft.end()) fonts->setProperty(k, (double) *it->second);
    }
    root->setProperty("fonts", juce::var(fonts));

    // spacing
    auto* spacing = new juce::DynamicObject();
    const char* spacingKeys[] = {"Xs","S","M","L","Xl"};
    auto& it2 = intTable();
    for (auto* k : spacingKeys) {
        auto f = it2.find(k);
        if (f != it2.end()) spacing->setProperty(k, *f->second);
    }
    root->setProperty("spacing", juce::var(spacing));

    // dimensions
    auto* dims = new juce::DynamicObject();
    const char* dimKeys[] = {"headerHeight","toolbarHeight","slotHeight","slotGap","slotPadding",
                             "trackPadding","trackStripWidth","iconSize","pillSize","pillGap",
                             "pillGroupGap","pillNameGap","chatBubblePad","chatGap","chatInputHeight"};
    for (auto* k : dimKeys) {
        auto f = it2.find(k);
        if (f != it2.end()) dims->setProperty(k, *f->second);
    }
    root->setProperty("dimensions", juce::var(dims));

    // radii
    auto* radii = new juce::DynamicObject();
    const char* radiiKeys[] = {"cornerRadius","cornerRadiusSm","cornerRadiusXs",
                               "pillRadius","activityDotSize","chatBubbleRadius"};
    for (auto* k : radiiKeys) {
        auto f = ft.find(k);
        if (f != ft.end()) radii->setProperty(k, (double) *f->second);
    }
    root->setProperty("radii", juce::var(radii));

    juce::var rootVar(root);
    auto json = juce::JSON::toString(rootVar, true);
    file.getParentDirectory().createDirectory();
    return file.replaceWithText(json);
}

void loadDefaultTheme() {
    // Restore compile-time defaults by overwriting each value.
    Color::bgApp           = 0xff161616;
    Color::bgPanel         = 0xff161616;
    Color::bgSurface       = 0xff1e1e1e;
    Color::bgSurfaceRaised = 0xff333333;
    Color::bgSlot          = 0xff2a2a2a;
    Color::bgControl       = 0xff2d2d2d;
    Color::bgControlHover  = 0xff333333;
    Color::bgListActive    = 0xff333333;
    Color::bgSelection     = 0xff262626;
    Color::bgStripe        = 0xff343434;
    Color::bgOverlay       = 0xff2a2a2a;
    Color::bgDisabled      = 0xff252525;
    Color::bgRecessed      = 0xff121212;
    Color::overlayDim      = 0x40000000;
    Color::border          = 0xff444444;
    Color::borderSubtle    = 0xff333333;
    Color::textPrimary     = 0xffd8d8d8;
    Color::textSecondary   = 0xffaaaaaa;
    Color::textDim         = 0xff666666;
    Color::textKeyHint     = 0xff909090;
    Color::textOnColor     = 0xffffffff;
    Color::controlHandle   = 0xffffffff;
    Color::accent          = 0xff2a6aaa;
    Color::accentDim       = 0xff1a4a6a;
    Color::activityOn      = 0xff44cc44;
    Color::activityOff     = 0xff1a3a1a;
    Color::statusError     = 0xff994444;
    Color::transportPlay      = 0xff2a6a2a;
    Color::transportRec       = 0xff6a2a2a;
    Color::transportRecDot    = 0xffcc4444;
    Color::transportCycle     = 0xff8a8a40;
    Color::transportCycleOff  = 0xff505050;
    Color::playhead           = 0xccffffff;
    Color::meterGreen      = 0xff44cc44;
    Color::meterAmber      = 0xffccaa44;
    Color::meterRed        = 0xffcc4444;
    Color::sendPeak        = 0xff1a6e1a;
    Color::typeInstrument  = 0xffaa8838;
    Color::typeAudio       = 0xff5080a0;
    Color::typeBus         = 0xffa86078;
    Color::typeOutput      = 0xff161616;
    Color::pillMute        = 0xff8a7a3a;
    Color::pillSolo        = 0xff3a6a8a;
    Color::pillArm         = 0xff8a4040;
    Color::pillInput       = 0xff8a4040;
    Color::pillTextOff     = 0xff888888;
    Color::slotInstrument  = 0xffddaa44;
    Color::slotEffect      = 0xff88aacc;
    Color::lcdDigit        = 0xffd0d0d0;
    Color::trackColors = {
        0xff2a5a3a, 0xff3a3a5a, 0xff4a3a2a, 0xff2a4a5a,
        0xff5a2a3a, 0xff3a5a2a, 0xff4a2a5a, 0xff5a4a2a,
    };
    Color::deviceColors = {
        0xff2a4a6a, 0xff4a3a5a, 0xff3a5a4a, 0xff5a3a3a,
        0xff3a4a3a, 0xff5a4a3a, 0xff3a3a5a, 0xff4a5a3a,
    };
    Color::chatUser        = 0xff2a4a6a;
    Color::chatAssistant   = 0xff2a2a2a;
    Color::chatTool        = 0xff1a2a1a;
    Color::chatError       = 0xff3a1a1a;
    Color::chatInput       = 0xff252525;

    fontSizeTitle    = 16.0f;
    fontSizeKeyHint  = 15.0f;
    fontSizeLg       = 14.0f;
    fontSizeMd       = 13.0f;
    fontSizeSm       = 12.0f;
    fontSizeXs       = 9.0f;
    fontSizePill     = 13.0f;
    fontSizeMeter    = 10.0f;
    fontSizeLcdLg    = 29.0f;
    fontSizeLcdMd    = 23.0f;
    fontSizeLcdLabel = 8.0f;

    spacingXs = 2; spacingS = 4; spacingM = 8; spacingL = 12; spacingXl = 16;

    headerHeight    = 28;
    toolbarHeight   = 32;
    slotHeight      = 24;
    slotGap         = 4;
    slotPadding     = 4;
    trackPadding    = 12;
    trackStripWidth = 160;
    iconSize        = 14;
    pillSize        = 20;
    pillRadius      = 4.0f;
    pillGap         = 5;
    pillGroupGap    = 8;
    pillNameGap     = 8;
    cornerRadius    = 6.0f;
    cornerRadiusSm  = 4.0f;
    cornerRadiusXs  = 3.0f;
    activityDotSize = 6.0f;
    chatBubbleRadius = 12.0f;
    chatBubblePad   = 12;
    chatGap         = 8;
    chatInputHeight = 40;

    currentTheme = "Minimal Dark";
}

juce::String currentThemeName() { return currentTheme; }

// --- Theme source: factory (BinaryData) + user (~/.config/performance/themes) ---

static juce::File userThemesDir() {
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile(".config/performance/themes");
}

std::vector<ThemeEntry> availableThemes() {
    std::map<juce::String, ThemeEntry> byId;

    // Factory themes
    for (int i = 0; i < kFactoryThemeCount; ++i) {
        int dataSize = 0;
        auto* data = BinaryData::getNamedResource(kFactoryThemes[i].binaryResourceName, dataSize);
        if (!data || dataSize <= 0) continue;
        auto parsed = juce::JSON::parse(juce::String::fromUTF8(data, dataSize));
        ThemeEntry e;
        e.id = kFactoryThemes[i].id;
        e.name = parsed.hasProperty("name") ? parsed["name"].toString() : e.id;
        e.description = parsed.hasProperty("description") ? parsed["description"].toString() : "";
        e.source = ThemeEntry::Source::Factory;
        byId[e.id] = std::move(e);
    }

    // User themes — scan directory; user overrides factory on id collision
    auto dir = userThemesDir();
    if (dir.exists()) {
        for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.json")) {
            auto id = f.getFileNameWithoutExtension();
            auto parsed = juce::JSON::parse(f);
            ThemeEntry e;
            e.id = id;
            e.name = parsed.hasProperty("name") ? parsed["name"].toString() : id;
            e.description = parsed.hasProperty("description") ? parsed["description"].toString() : "";
            e.source = ThemeEntry::Source::User;
            e.path = f;
            byId[e.id] = std::move(e);
        }
    }

    std::vector<ThemeEntry> result;
    result.reserve(byId.size());
    for (auto& [_, entry] : byId)
        result.push_back(std::move(entry));
    return result;
}

bool loadThemeById(const juce::String& id) {
    // User themes dir first (user overrides factory)
    auto dir = userThemesDir();
    auto userFile = dir.getChildFile(id + ".json");
    if (userFile.existsAsFile()) {
        auto parsed = juce::JSON::parse(userFile);
        if (loadThemeFromVar(parsed)) {
            juce::Logger::writeToLog("[Theme] Loaded user theme '" + currentTheme
                                     + "' from " + userFile.getFullPathName());
            return true;
        }
    }

    // Factory themes
    for (int i = 0; i < kFactoryThemeCount; ++i) {
        if (id != juce::String(kFactoryThemes[i].id)) continue;
        int dataSize = 0;
        auto* data = BinaryData::getNamedResource(kFactoryThemes[i].binaryResourceName, dataSize);
        if (!data || dataSize <= 0) continue;
        auto parsed = juce::JSON::parse(juce::String::fromUTF8(data, dataSize));
        if (loadThemeFromVar(parsed)) {
            juce::Logger::writeToLog("[Theme] Loaded factory theme '" + currentTheme + "'");
            return true;
        }
    }

    juce::Logger::writeToLog("[Theme] Theme not found: " + id);
    return false;
}

void ensureDefaultThemeFile() {
    // Optionally write the factory themes to the user dir as starter files.
    auto dir = userThemesDir();
    if (! dir.exists()) dir.createDirectory();
}

} // namespace Theme

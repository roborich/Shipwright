// SOH [Unbound] Message tables are owned, growable and hash-indexed; mods can add message ids.
// See unbound-docs/text.md.
#include "z_message_OTR.h"
#include <libultraship/libultraship.h>
#include "soh/resource/type/Scene.h"
#include "soh/unbound/UnboundJson.h"
#include <ship/utils/StringHelper.h>
#include "global.h"
#include "vt.h"
#include "soh/resource/type/Text.h"
#include <message_data_static.h>
#include "Enhancements/custom-message/CustomMessageManager.h"
#include "Enhancements/custom-message/CustomMessageTypes.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <array>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" MessageTableEntry* sNesMessageEntryTablePtr;
extern "C" MessageTableEntry* sGerMessageEntryTablePtr;
extern "C" MessageTableEntry* sFraMessageEntryTablePtr;
extern "C" MessageTableEntry* sJpnMessageEntryTablePtr;
extern "C" MessageTableEntry* sStaffMessageEntryTablePtr;
extern "C" char* _message_0xFFFC_nes;

namespace {

constexpr uint16_t kTerminatorId = 0xFFFF;
constexpr char kMessageEnd = '\x02';

enum MessageLanguage { MSG_NES, MSG_GER, MSG_FRA, MSG_JPN, MSG_STAFF, MSG_LANGUAGE_COUNT };

struct LanguageSpec {
    MessageLanguage language;
    const char* jsonName;    // "language" value in unbound/text/*.json
    const char* folder;      // archive folder, also the override/ subfolder
    const char* baseFile;    // primary base resource
    const char* altBaseFile; // fallback base resource (NTSC english), may be null
    uint16_t firstId;        // asserted first id of the base table
};

const LanguageSpec kLanguages[MSG_LANGUAGE_COUNT] = {
    { MSG_NES, "eng", "text/nes_message_data_static", "text/nes_message_data_static/nes_message_data_static",
      "text/nes_message_data_static/ntsc_nes_message_data_static", 0x0001 },
    { MSG_GER, "ger", "text/ger_message_data_static", "text/ger_message_data_static/ger_message_data_static", nullptr,
      0x0001 },
    { MSG_FRA, "fra", "text/fra_message_data_static", "text/fra_message_data_static/fra_message_data_static", nullptr,
      0x0001 },
    { MSG_JPN, "jpn", "text/jpn_message_data_static", "text/jpn_message_data_static/jpn_message_data_static", nullptr,
      0x0001 },
    { MSG_STAFF, "staff", "text/staff_message_data_static", "text/staff_message_data_static/staff_message_data_static",
      nullptr, 0x0500 },
};

/**
 * One language's message table. Owns the message bytes (deque keeps c_str() stable across growth),
 * keeps the C-visible entry array in base order with additions appended before the terminator, and
 * a hash index for lookup.
 */
struct MessageTable {
    std::deque<std::string> storage;
    std::vector<MessageTableEntry> entries; // published without terminator until Finalize()
    std::unordered_map<uint16_t, size_t> index;
    bool loaded = false;

    void Set(uint16_t id, uint8_t typePos, std::string bytes) {
        if (id == kTerminatorId) {
            return;
        }
        if (bytes.empty() || bytes.back() != kMessageEnd) {
            bytes.push_back(kMessageEnd);
        }
        storage.push_back(std::move(bytes));
        const std::string& owned = storage.back();

        MessageTableEntry entry;
        entry.textId = id;
        entry.typePos = typePos;
        entry.segment = owned.c_str();
        entry.msgSize = owned.size();

        auto it = index.find(id);
        if (it != index.end()) {
            entries[it->second] = entry;
        } else {
            index[id] = entries.size();
            entries.push_back(entry);
        }
    }

    void Set(const SOH::MessageEntry& msg) {
        Set(msg.id, (uint8_t)((msg.textboxType << 4) | msg.textboxYPos), msg.msg);
    }

    MessageTableEntry* Find(uint16_t id) {
        auto it = index.find(id);
        return it == index.end() ? nullptr : &entries[it->second];
    }

    MessageTableEntry* Finalize() {
        MessageTableEntry terminator = { kTerminatorId, 0, nullptr, 0 };
        entries.push_back(terminator);
        loaded = true;
        return entries.data();
    }
};

std::array<MessageTable, MSG_LANGUAGE_COUNT> sTables;
bool sInitialized = false;

std::shared_ptr<SOH::Text> LoadTextResource(const std::string& path) {
    return std::static_pointer_cast<SOH::Text>(Ship::Context::GetInstance()->GetResourceManager()->LoadResource(path));
}

bool LoadJsonMessageFile(const std::string& path, const LanguageSpec* expected);

// Unbound archives carry the base table as text/<lang>/messages.json (unbound-docs/text.md).
bool LoadJsonBase(const LanguageSpec& spec) {
    std::string jsonBase = std::string("text/") + spec.jsonName + "/messages.json";
    if (!Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->HasFile(jsonBase)) {
        return false;
    }
    return LoadJsonMessageFile(jsonBase, &spec);
}

bool LoadBase(MessageTable& table, const LanguageSpec& spec) {
    if (LoadJsonBase(spec)) {
        return true;
    }

    auto file = LoadTextResource(spec.baseFile);
    if (file == nullptr && spec.altBaseFile != nullptr) {
        file = LoadTextResource(spec.altBaseFile);
    }
    if (file == nullptr) {
        return false;
    }
    for (const auto& msg : file->messages) {
        table.Set(msg);
    }
    if (table.entries.empty() || table.entries[0].textId != spec.firstId) {
        SPDLOG_WARN("[Unbound] {} does not start at message {:#06x}", spec.baseFile, spec.firstId);
    }
    return true;
}

// override/<folder>/* : Text resources whose entries replace OR add ids (vanilla SoH could only replace)
void LoadOverrides(MessageTable& table, const LanguageSpec& spec) {
    auto files = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->ListFiles(
        std::string("override/") + spec.folder + "/*");
    if (files == nullptr) {
        return;
    }
    for (const auto& path : *files) {
        auto file = LoadTextResource(path);
        if (file == nullptr) {
            continue;
        }
        for (const auto& msg : file->messages) {
            table.Set(msg);
        }
    }
}

// JSON strings carry message bytes as code points 0-255 (Latin-1 mapping); control codes included.
// Code points above U+00FF have no byte representation; each becomes '?' and is counted in `replaced`.
std::string JsonTextToBytes(const std::string& utf8, size_t& replaced) {
    constexpr char kReplacement = '?';
    std::string bytes;
    bytes.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size();) {
        unsigned char c = utf8[i];
        if (c < 0x80) {
            bytes.push_back((char)c);
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
            uint32_t cp = ((c & 0x1F) << 6) | (utf8[i + 1] & 0x3F);
            bytes.push_back((char)(cp & 0xFF));
            i += 2;
        } else {
            bytes.push_back(kReplacement);
            replaced++;
            i += (c & 0xF0) == 0xE0 ? 3 : 4;
        }
    }
    return bytes;
}

uint16_t ParseMessageId(const nlohmann::json& value) {
    return (uint16_t)SOH::Unbound::ToInt(value, kTerminatorId);
}

MessageTable* TableForJsonLanguage(const std::string& name) {
    for (const auto& spec : kLanguages) {
        if (name == spec.jsonName || (spec.language == MSG_NES && name == "nes")) {
            return &sTables[spec.language];
        }
    }
    return nullptr;
}

void ApplyJsonMessage(MessageTable& table, const nlohmann::json& entry, uint16_t id, const std::string& path) {
    uint8_t box = (uint8_t)SOH::Unbound::ToInt(entry.value("box", nlohmann::json(0)));
    uint8_t ypos = (uint8_t)SOH::Unbound::ToInt(entry.value("ypos", nlohmann::json(0)));
    size_t replaced = 0;
    std::string bytes = JsonTextToBytes(entry.at("text").get<std::string>(), replaced);
    if (replaced > 0) {
        SPDLOG_WARN("[Unbound] {}: message {:#06x} has {} character(s) outside U+0000-U+00FF, written as '?'", path, id,
                    replaced);
    }
    table.Set(id, (uint8_t)((box << 4) | ypos), std::move(bytes));
}

// "messages": [ {id, box, ypos, text}, ... ]  or  { "<id>": {box, ypos, text}, ... }
size_t ApplyJsonMessages(MessageTable& table, const nlohmann::json& messages, const std::string& path) {
    size_t count = 0;
    if (messages.is_array()) {
        for (const auto& entry : messages) {
            ApplyJsonMessage(table, entry, ParseMessageId(entry.at("id")), path);
            count++;
        }
    } else {
        for (const auto& [key, entry] : messages.items()) {
            ApplyJsonMessage(table, entry, ParseMessageId(nlohmann::json(key)), path);
            count++;
        }
    }
    return count;
}

// { "language": "eng", "messages": ... }. When `expected` is set the file must declare that language
// (base tables); otherwise any language is accepted (unbound/text merge files).
bool LoadJsonMessageFile(const std::string& path, const LanguageSpec* expected) {
    auto archiveManager = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    auto file = archiveManager->LoadFile(path);
    if (file == nullptr || file->Buffer == nullptr) {
        SPDLOG_ERROR("[Unbound] could not read {}", path);
        return false;
    }
    try {
        auto doc = nlohmann::json::parse(file->Buffer->begin(), file->Buffer->end(), nullptr, true, true);
        std::string language = doc.at("language").get<std::string>();
        MessageTable* table = TableForJsonLanguage(language);
        if (table == nullptr) {
            SPDLOG_ERROR("[Unbound] {}: unknown language '{}'", path, language);
            return false;
        }
        if (expected != nullptr && table != &sTables[expected->language]) {
            SPDLOG_ERROR("[Unbound] {}: declares language '{}', expected '{}'", path, language, expected->jsonName);
            return false;
        }
        size_t count = ApplyJsonMessages(*table, doc.at("messages"), path);
        SPDLOG_INFO("[Unbound] {}: {} message(s)", path, count);
        return count > 0;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Unbound] {}: {}", path, e.what());
        return false;
    }
}

void LoadJsonMessages() {
    auto files = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->ListFiles("unbound/text/*");
    if (files == nullptr) {
        return;
    }
    for (const auto& path : *files) {
        if (path.ends_with(".json")) {
            LoadJsonMessageFile(path, nullptr);
        }
    }
}

void PublishTables() {
    sNesMessageEntryTablePtr = sTables[MSG_NES].loaded ? sTables[MSG_NES].entries.data() : nullptr;
    sGerMessageEntryTablePtr = sTables[MSG_GER].loaded ? sTables[MSG_GER].entries.data() : nullptr;
    sFraMessageEntryTablePtr = sTables[MSG_FRA].loaded ? sTables[MSG_FRA].entries.data() : nullptr;
    sJpnMessageEntryTablePtr = sTables[MSG_JPN].loaded ? sTables[MSG_JPN].entries.data() : nullptr;
    sStaffMessageEntryTablePtr = sTables[MSG_STAFF].loaded ? sTables[MSG_STAFF].entries.data() : nullptr;

    MessageTableEntry* fffc = sTables[MSG_NES].loaded ? sTables[MSG_NES].Find(0xFFFC) : nullptr;
    _message_0xFFFC_nes = fffc != nullptr ? (char*)fffc->segment : nullptr;
}

} // namespace

extern "C" void OTRMessage_Init(void) {
    if (sInitialized) {
        return; // the tables are process-lifetime; a second pass would append a second terminator
    }
    sInitialized = true;

    // Base + override/ per language, then JSON merge files across all languages, then publish.
    // JSON runs after every base so a mod file can carry several languages.
    for (const auto& spec : kLanguages) {
        MessageTable& table = sTables[spec.language];
        if (LoadBase(table, spec)) {
            LoadOverrides(table, spec);
        }
    }
    LoadJsonMessages();
    for (auto& table : sTables) {
        if (!table.entries.empty()) {
            table.Finalize();
        }
    }
    PublishTables();
}

extern "C" MessageTableEntry* OTRMessage_Find(MessageTableEntry* table, u16 textId) {
    for (auto& t : sTables) {
        if (t.loaded && t.entries.data() == table) {
            return t.Find(textId);
        }
    }
    return nullptr;
}

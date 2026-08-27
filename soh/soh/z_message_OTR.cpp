// SOH [Unbound] Message tables are owned, growable and hash-indexed; mods can add message ids.
// See unbound-docs/text.md.
#include <libultraship/libultraship.h>
#include "soh/resource/type/Scene.h"
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
    const char* jsonName;   // "language" value in unbound/text/*.json
    const char* folder;     // archive folder, also the override/ subfolder
    const char* baseFile;   // primary base resource
    const char* altBaseFile; // fallback base resource (NTSC english), may be null
    uint16_t firstId;       // asserted first id of the base table
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
    { MSG_STAFF, "staff", "text/staff_message_data_static",
      "text/staff_message_data_static/staff_message_data_static", nullptr, 0x0500 },
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

std::shared_ptr<SOH::Text> LoadTextResource(const std::string& path) {
    return std::static_pointer_cast<SOH::Text>(
        Ship::Context::GetInstance()->GetResourceManager()->LoadResource(path));
}

bool LoadJsonMessageFile(const std::string& path);

bool LoadBase(MessageTable& table, const LanguageSpec& spec) {
    // Unbound archives carry the base table as JSON (unbound-docs/text.md)
    std::string jsonBase = std::string("text/") + spec.jsonName + "/messages.json";
    if (Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->HasFile(jsonBase)) {
        if (LoadJsonMessageFile(jsonBase)) {
            return !table.entries.empty();
        }
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
std::string JsonTextToBytes(const std::string& utf8) {
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
            // code points above U+00FF have no byte representation; keep the low byte and move on
            size_t len = (c & 0xF0) == 0xE0 ? 3 : 4;
            bytes.push_back((char)c);
            i += len;
        }
    }
    return bytes;
}

uint16_t ParseMessageId(const nlohmann::json& value) {
    if (value.is_number_integer()) {
        return (uint16_t)value.get<int>();
    }
    return (uint16_t)std::stoi(value.get<std::string>(), nullptr, 0);
}

MessageTable* TableForJsonLanguage(const std::string& name) {
    for (const auto& spec : kLanguages) {
        if (name == spec.jsonName || (spec.language == MSG_NES && name == "nes")) {
            return &sTables[spec.language];
        }
    }
    return nullptr;
}

void ApplyJsonMessage(MessageTable& table, const nlohmann::json& entry, uint16_t id) {
    uint8_t box = (uint8_t)entry.value("box", 0);
    uint8_t ypos = (uint8_t)entry.value("ypos", 0);
    table.Set(id, (uint8_t)((box << 4) | ypos), JsonTextToBytes(entry.at("text").get<std::string>()));
}

// unbound/text/*.json : { "language": "eng", "messages": [ {id, box, ypos, text}, ... ] }
// or "messages": { "<id>": {box, ypos, text}, ... }
bool LoadJsonMessageFile(const std::string& path) {
    auto archiveManager = Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager();
    auto file = archiveManager->LoadFile(path);
    if (file == nullptr || file->Buffer == nullptr) {
        SPDLOG_ERROR("[Unbound] could not read {}", path);
        return false;
    }
    try {
        auto doc = nlohmann::json::parse(file->Buffer->begin(), file->Buffer->end(), nullptr, true, true);
        MessageTable* table = TableForJsonLanguage(doc.at("language").get<std::string>());
        if (table == nullptr) {
            SPDLOG_ERROR("[Unbound] {}: unknown language '{}'", path, doc["language"].get<std::string>());
            return false;
        }
        const auto& messages = doc.at("messages");
        size_t count = 0;
        if (messages.is_array()) {
            for (const auto& entry : messages) {
                ApplyJsonMessage(*table, entry, ParseMessageId(entry.at("id")));
                count++;
            }
        } else {
            for (const auto& [key, entry] : messages.items()) {
                ApplyJsonMessage(*table, entry, (uint16_t)std::stoi(key, nullptr, 0));
                count++;
            }
        }
        SPDLOG_INFO("[Unbound] {}: {} message(s)", path, count);
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Unbound] {}: {}", path, e.what());
        return false;
    }
}

void LoadJsonMessages() {
    auto files =
        Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->ListFiles("unbound/text/*");
    if (files == nullptr) {
        return;
    }
    for (const auto& path : *files) {
        if (path.ends_with(".json")) {
            LoadJsonMessageFile(path);
        }
    }
}

MessageTableEntry* BuildLanguage(const LanguageSpec& spec) {
    MessageTable& table = sTables[spec.language];
    if (!LoadBase(table, spec)) {
        return nullptr;
    }
    LoadOverrides(table, spec);
    return table.Finalize();
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

extern "C" void OTRMessage_Init() {
    if (sTables[MSG_NES].loaded) {
        return; // already built; the tables are process-lifetime
    }

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

/**
 * Hash lookup for any published table pointer. Returns NULL when the id is absent or the table is unknown.
 */
extern "C" MessageTableEntry* OTRMessage_Find(MessageTableEntry* table, u16 textId) {
    for (auto& t : sTables) {
        if (t.loaded && t.entries.data() == table) {
            return t.Find(textId);
        }
    }
    return nullptr;
}

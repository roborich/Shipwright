// SOH [Unbound] Message tables are owned, growable and hash-indexed; mods can add message ids.
// See unbound-docs/text.md.
#include "z_message_OTR.h"
#include <libultraship/libultraship.h>
#include "soh/resource/type/Scene.h"
#include "soh/unbound/UnboundJson.h"
#include "soh/unbound/UnboundSchema.h"
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
    const char* jsonName;    // text/<jsonName>/messages.json (unbound-docs/text.md)
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

bool LoadJsonBase(MessageTable& table, const LanguageSpec& spec);

bool LoadBase(MessageTable& table, const LanguageSpec& spec) {
    if (LoadJsonBase(table, spec)) {
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

// "messages": { "<id>": { box, ypos, text } }. A null entry is a deletion left by a single-layer document.
size_t ApplyJsonMessages(MessageTable& table, const nlohmann::json& messages, const std::string& path) {
    size_t count = 0;
    for (const auto& [key, entry] : messages.items()) {
        if (entry.is_object()) {
            ApplyJsonMessage(table, entry, ParseMessageId(nlohmann::json(key)), path);
            count++;
        }
    }
    return count;
}

// text/<lang>/messages.json, layer-merged across every mounted archive: the converted base table plus each
// mod's additions, replacements and deletions (unbound-docs/text.md).
bool LoadJsonBase(MessageTable& table, const LanguageSpec& spec) {
    namespace K = SOH::Unbound::Schema;
    std::string path = std::string(K::kMessagesPathPrefix) + spec.jsonName + K::kMessagesPathSuffix;
    nlohmann::json doc = SOH::Unbound::LoadMergedJson(path);
    if (!doc.is_object()) {
        return false;
    }
    try {
        size_t count = ApplyJsonMessages(table, doc.value(K::kMessages, nlohmann::json::object()), path);
        SPDLOG_INFO("[Unbound] {}: {} message(s)", path, count);
        return true; // an empty (or fully deleted) table is still the base; do not fall through to the binary one
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Unbound] {}: {}", path, e.what());
        return false;
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

    // Per language: base (merged JSON, or the legacy Text resource) + legacy override/ resources, then publish.
    for (const auto& spec : kLanguages) {
        MessageTable& table = sTables[spec.language];
        if (LoadBase(table, spec)) {
            LoadOverrides(table, spec);
        }
    }
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

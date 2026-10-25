#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>

#include "soh/resource/type/Text.h"
#include <algorithm>

extern "C" {
#include <message_data_static.h>
}

extern "C" MessageTableEntry* sNesMessageEntryTablePtr;
extern "C" MessageTableEntry* sGerMessageEntryTablePtr;
extern "C" MessageTableEntry* sFraMessageEntryTablePtr;
extern "C" MessageTableEntry* sJpnMessageEntryTablePtr;
extern "C" MessageTableEntry* sStaffMessageEntryTablePtr;
// extern "C" MessageTableEntry* _message_0xFFFC_nes;

static MessageTableEntry* BuildTable(const std::vector<MessageTableEntry>& entries) {
    // OTRTODO: Should not be malloc'ing here. It's fine for now since we check elsewhere that the message table is
    // already null.
    MessageTableEntry* table = (MessageTableEntry*)malloc(sizeof(MessageTableEntry) * entries.size());

    std::copy(entries.begin(), entries.end(), table);

    return table;
}

static MessageTableEntry BuildEntry(const SOH::MessageEntry& msg) {
    MessageTableEntry entry;
    entry.textId = msg.id;
    entry.typePos = (msg.textboxType << 4) | msg.textboxYPos;
    entry.segment = msg.msg.c_str();
    entry.msgSize = static_cast<u32>(msg.msg.size());
    return entry;
}

static std::vector<MessageTableEntry> BuildEntries(const std::vector<SOH::MessageEntry>& messages) {
    std::vector<MessageTableEntry> entries;
    entries.reserve(messages.size());

    for (auto& msg : messages) {
        entries.push_back(BuildEntry(msg));
    }

    return entries;
}

static void ReplaceEntry(std::vector<MessageTableEntry>& entries, const SOH::MessageEntry& msg) {
    auto existing = std::find_if(entries.begin(), entries.end(),
                                 [&](const MessageTableEntry& entry) { return entry.textId == msg.id; });

    if (existing != entries.end()) {
        *existing = BuildEntry(msg);
    }
}

static void ApplyOverrides(std::vector<MessageTableEntry>& entries, const std::string& folder) {
    auto resourceManager = Ship::Context::GetRawInstance()->GetResourceManager();
    auto paths = resourceManager->GetArchiveManager()->ListFiles(folder + "/*");

    for (auto& path : *paths) {
        auto file = std::static_pointer_cast<SOH::Text>(resourceManager->LoadResource(path));

        for (auto& msg : file->messages) {
            ReplaceEntry(entries, msg);
        }
    }
}

MessageTableEntry* OTRMessage_LoadTable(const std::string& filePath, bool isNES, uint16_t firstTextId) {
    auto file = std::static_pointer_cast<SOH::Text>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(filePath));

    if (file == nullptr)
        return nullptr;

    std::vector<MessageTableEntry> entries = BuildEntries(file->messages);
    ApplyOverrides(entries, "override/" + filePath.substr(0, filePath.find_last_of('/')));

    if (isNES) {
        auto entry = std::find_if(entries.begin(), entries.end(),
                                  [](const MessageTableEntry& entry) { return entry.textId == 0xFFFC; });
        if (entry != entries.end()) {
            _message_0xFFFC_nes = (char*)entry->segment;
        }
    }

    // Assert that the first message starts at the first text ID
    assert(!entries.empty() && entries[0].textId == firstTextId);

    return BuildTable(entries);
}

extern "C" void OTRMessage_Init() {
    // OTRTODO: Added a lot of null checks here so that we don't malloc the table multiple times causing a memory leak.
    // We really ought to fix the implementation such that we aren't malloc'ing new tables.
    // Once we fix the implementation, remove these NULL checks.
    if (sNesMessageEntryTablePtr == NULL) {
        sNesMessageEntryTablePtr =
            OTRMessage_LoadTable("text/nes_message_data_static/nes_message_data_static", true, 0x0001);
    }
    if (sGerMessageEntryTablePtr == NULL) {
        sGerMessageEntryTablePtr =
            OTRMessage_LoadTable("text/ger_message_data_static/ger_message_data_static", false, 0x0001);
    }
    if (sFraMessageEntryTablePtr == NULL) {
        sFraMessageEntryTablePtr =
            OTRMessage_LoadTable("text/fra_message_data_static/fra_message_data_static", false, 0x0001);
    }
    if (sJpnMessageEntryTablePtr == NULL) {
        sJpnMessageEntryTablePtr =
            OTRMessage_LoadTable("text/jpn_message_data_static/jpn_message_data_static", false, 0x0001);
    }
    // Note: Make sure this loads after PAL nes_message_data_static, so that message 0xFFFC is definitely loaded if it
    // exists
    if (sNesMessageEntryTablePtr == NULL) {
        sNesMessageEntryTablePtr =
            OTRMessage_LoadTable("text/nes_message_data_static/ntsc_nes_message_data_static", false, 0x0001);
    }

    if (sStaffMessageEntryTablePtr == NULL) {
        sStaffMessageEntryTablePtr =
            OTRMessage_LoadTable("text/staff_message_data_static/staff_message_data_static", false, 0x0500);
    }
}

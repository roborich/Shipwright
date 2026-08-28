#pragma once
// SOH [Unbound] Owned, growable, hash-indexed message tables. Format: unbound-docs/SPEC.md §5.
#include "z64.h"
#include "message_data_static.h"

#ifdef __cplusplus
extern "C" {
#endif

// Builds every language table (base resource, override/, unbound/text JSON) and publishes the
// s*MessageEntryTablePtr globals. Idempotent.
void OTRMessage_Init(void);

// Hash lookup into a published table pointer. NULL when the id is absent or the table is unknown.
MessageTableEntry* OTRMessage_Find(MessageTableEntry* table, u16 textId);

#ifdef __cplusplus
}
#endif

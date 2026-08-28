# Unbound: text

Lets mods **add** message ids (not just replace them), ship text as a small JSON merge file, and
show longer messages. The document (`text/<lang>/messages.json`: keys, id rules, byte encoding)
is defined in [`SPEC.md`](./SPEC.md) §5; this file covers why and how. Overview in
[`README.md`](./README.md).

## The caps, and why they exist

- Each language's table was a `malloc`'d `MessageTableEntry[]` built once from one binary `Text`
  resource per language (`text/<lang>_message_data_static/…`), terminated by id `0xFFFF`.
- `override/text/<lang>_message_data_static/*` merged by id but could only **replace** — the table
  had no room to grow, so a new id meant re-shipping the whole ~1 400-entry resource (the Prelude
  "message" edit kind does exactly that today).
- `Message_FindMessage` / `…JPN` / `…CreditsMessage` were linear scans.
- `Font.msgBuf[1280]` was filled by an unclamped `memcpy` of the message; `msgBufDecoded[200]`
  bounded a decoded textbox.

## What changes

### `soh/soh/z_message_OTR.cpp` (rewritten)

One `MessageTable` per language (`eng`/`nes`, `ger`, `fra`, `jpn`, `staff`):

- Owns its bytes (`std::deque<std::string>` so `c_str()` stays stable), keeps the C-visible
  `std::vector<MessageTableEntry>` in base order with new ids appended before the terminator, and
  a `textId → index` hash.
- Load order per language: base (`LoadJsonBase` — the layer-merged `text/<lang>/messages.json`
  through `Unbound::LoadMergedJson` when any mounted archive provides it, otherwise the binary/XML
  `Text` resource) → `override/<folder>/*` (legacy binary overrides, **add or replace**) →
  finalize. Initialisation runs once per process (`sInitialized`). An empty merged table is still
  the base; it does not fall through to the binary resource.
- `messages` entries whose value is `null` are deletions: the merge drops them from upper layers
  and `ApplyJsonMessages` skips non-objects, so the id never reaches the table.
- Publishes the same `sNes/Ger/Fra/Jpn/StaffMessageEntryTablePtr` globals, so the ~15 existing
  consumers (message viewer, settings menu, save editor, kanji font, custom message manager)
  compile unchanged and still see a `0xFFFF`-terminated array.
- `OTRMessage_Find(table, id)` — hash lookup for any published table pointer; used by the three
  find functions in `z_message_PAL.c`, which keep their vanilla not-found fallbacks.
- `JsonTextToBytes` maps each code point U+0000–U+00FF to one byte; anything higher becomes `?`
  with one warning per message (SPEC §5).

Illustrative merge file (the contract is SPEC §5):

```json
{ "messages": { "0x0F12": { "box": 0, "ypos": 0, "text": "Hello, modded world." } } }
```

Mods no longer bundle a language's whole table: a Prelude message edit becomes a `messages.json`
with the changed ids. Adding an id used by a custom actor or scene is a one-line entry.

### Buffers (`z64.h`)

`MESSAGE_BUF_SIZE` 8192 (was 1280) and `MESSAGE_DECODED_BUF_SIZE` 1024 (was 200). The three raw
copies into `msgBuf` (`Message_OpenText` ×2, `z_kanfont.c`) clamp to the buffer and log when they
truncate. A single textbox is still bounded by the decoded buffer; the vanilla box-break control
codes remain the way to page long text.

## Not changed

- Legacy archives keep their binary/XML `Text` base resources — Prelude's OTXT codec keeps working,
  and the `override/` mechanism still applies on top of either base.
- `CustomMessageManager` (randomizer / enhancement text generated at runtime) still bypasses the
  tables via `VB`/`OnOpenText`; it neither needed nor gets a change.
- Message ids stay `u16`; the sentinel ids are reserved (SPEC §5).

## Status

Implemented on the `unbound` branch. See the README status table for build/verification state.

## Verification

1. Vanilla parity: talk to an NPC in each language (the settings menu language switch), open the
   credits text, check the Message Viewer dev window lists the tables.
2. Add: an archive with `text/eng/messages.json` adding id `0x0F12`; `Message_StartTextbox` it via
   the Message Viewer / a modded actor — it displays.
3. Replace: the same file overriding an existing id (e.g. `0x0001`) — the new text shows.
4. Delete: the same file with `"0x0001": null` — the vanilla not-found fallback shows.
5. Long: a message > 1280 bytes with box breaks pages correctly; one > 8192 bytes logs a truncation
   line instead of corrupting memory.

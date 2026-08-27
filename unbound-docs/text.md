# Unbound: text

Lets mods **add** message ids (not just replace them), ship text as a small JSON merge file, and
show longer messages. Overview in [`README.md`](./README.md).

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
- Load order per language: base resource → `override/<folder>/*` (**add or replace**) →
  `unbound/text/*.json` (add or replace, any language per file) → finalize.
- Publishes the same `sNes/Ger/Fra/Jpn/StaffMessageEntryTablePtr` globals, so the ~15 existing
  consumers (message viewer, settings menu, save editor, kanji font, custom message manager)
  compile unchanged and still see a `0xFFFF`-terminated array.
- `OTRMessage_Find(table, id)` — hash lookup for any published table pointer; used by the three
  find functions in `z_message_PAL.c`, which keep their vanilla not-found fallbacks.

### JSON merge files

`unbound/text/<anything>.json` in any loaded archive:

```json
{
  "language": "eng",
  "messages": [
    { "id": "0x0F12", "box": 0, "ypos": 0, "text": "Hello, modded world." },
    { "id": 3859,     "box": 2, "ypos": 1, "text": "Second message" }
  ]
}
```

- `language`: `eng` (or `nes`), `ger`, `fra`, `jpn`, `staff`.
- `messages`: array of entries, or an object keyed by id (`"0x0F12": { … }`).
- `id`: integer or a string parsed with base auto-detect (`"0x0F12"`, `"3858"`).
- `box` / `ypos`: textbox type and y-position (the `typePos` nibbles).
- `text`: the raw message bytes as a JSON string where each code point `0–255` is one byte —
  control codes (`` newline, `A` colour, `` end, …) are written as escapes.
  A missing `` terminator is appended.

Mods no longer bundle a language's whole table: a Prelude message edit becomes a JSON file with
the changed ids. Adding an id used by a custom actor or scene is a one-line entry.

### Buffers (`z64.h`)

`MESSAGE_BUF_SIZE` 8192 (was 1280) and `MESSAGE_DECODED_BUF_SIZE` 1024 (was 200). The three raw
copies into `msgBuf` (`Message_OpenText` ×2, `z_kanfont.c`) clamp to the buffer and log when they
truncate. A single textbox is still bounded by the decoded buffer; the vanilla box-break control
codes remain the way to page long text.

## Not changed

- The base resources stay the binary/XML `Text` type — Prelude's OTXT codec keeps working, and the
  `override/` mechanism now covers additions too.
- `CustomMessageManager` (randomizer / enhancement text generated at runtime) still bypasses the
  tables via `VB`/`OnOpenText`; it neither needed nor gets a change.
- Message ids stay `u16`. `0xFFFC`/`0xFFFD`/`0xFFFF` keep their sentinel roles.

## Status

Implemented on the `unbound` branch. See the README status table for build/verification state.

## Verification

1. Vanilla parity: talk to an NPC in each language (the settings menu language switch), open the
   credits text, check the Message Viewer dev window lists the tables.
2. Add: an archive with `unbound/text/test.json` adding id `0x0F12`; `Message_StartTextbox` it via
   the Message Viewer / a modded actor — it displays.
3. Replace: the same file overriding an existing id (e.g. `0x0001`) — the new text shows.
4. Long: a message > 1280 bytes with box breaks pages correctly; one > 8192 bytes logs a truncation
   line instead of corrupting memory.

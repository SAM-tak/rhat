// L^ (lhat) -- LSP server: byte offset -> LSP Position.

#include "position.h"

// One code point from `text[*at..)`, `*at` moved past it. Malformed input is
// read one byte at a time, same as error.c's next_character -- but this
// counts UTF-16 units rather than terminal cells, so it is not the same
// function wearing a different name; the two answer different questions
// from the same kind of walk.
static uint32_t decode_and_advance(const char *text, size_t length, size_t *at)
{
    size_t i = *at;
    unsigned char lead = (unsigned char)text[i];
    size_t follow = 0;
    uint32_t code = lead;

    if (lead >= 0xF0u) {
        follow = 3;
        code = lead & 0x07u;
    }
    else if (lead >= 0xE0u) {
        follow = 2;
        code = lead & 0x0Fu;
    }
    else if (lead >= 0xC0u) {
        follow = 1;
        code = lead & 0x1Fu;
    }
    if (i + follow >= length) {
        *at = i + 1;
        return lead;
    }
    for (size_t k = 1; k <= follow; k++) {
        unsigned char byte = (unsigned char)text[i + k];
        if ((byte & 0xC0u) != 0x80u) {
            *at = i + 1;
            return lead;
        }
        code = (code << 6) | (byte & 0x3Fu);
    }
    *at = i + follow + 1;
    return code;
}

uint32_t lsp_offset_at(const char *text, size_t length, int line, int character)
{
    size_t at = 0;
    for (int row = 0; row < line && at < length;) {
        if (text[at] == '\r') {
            at++;
            if (at < length && text[at] == '\n') {
                at++;
            }
            row++;
        }
        else if (text[at++] == '\n') {
            row++;
        }
    }
    int units = 0;
    while (at < length && text[at] != '\n' && text[at] != '\r' && units < character) {
        size_t next = at;
        uint32_t code = decode_and_advance(text, length, &next);
        int width = code >= 0x10000u ? 2 : 1;
        if (units + width > character) {
            break;
        }
        at = next;
        units += width;
    }
    return (uint32_t)at;
}

LspPosition lsp_position_at(const char *text, size_t length, uint32_t offset)
{
    LspPosition position = {0};
    size_t at = 0, limit = offset < length ? offset : length;
    while (at < limit) {
        if (text[at] == '\r') {
            at++;
            if (at < limit && text[at] == '\n') {
                at++;
            }
            position.line++;
            position.character = 0;
        }
        else if (text[at] == '\n') {
            at++;
            position.line++;
            position.character = 0;
        }
        else {
            uint32_t code = decode_and_advance(text, length, &at);
            position.character += code >= 0x10000u ? 2 : 1;
        }
    }
    return position;
}

// ---------------------------------------------------------------------------
// The wire form
// ---------------------------------------------------------------------------

cJSON *lsp_position_json(LspPosition at)
{
    cJSON *object = cJSON_CreateObject();
    if (object != NULL) {
        cJSON_AddNumberToObject(object, "line", at.line);
        cJSON_AddNumberToObject(object, "character", at.character);
    }
    return object;
}

cJSON *lsp_range_json(LspPosition from, LspPosition to)
{
    cJSON *range = cJSON_CreateObject();
    if (range != NULL) {
        cJSON_AddItemToObject(range, "start", lsp_position_json(from));
        cJSON_AddItemToObject(range, "end", lsp_position_json(to));
    }
    return range;
}

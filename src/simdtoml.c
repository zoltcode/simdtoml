#include "simdtoml.h"


static const uint8_t TOML_WHITESPACE_LUT[256] = {
    [' ']  = 1, /* Index 32 (Space)      -> Marker 1 */
    ['\t'] = 1, /* Index 9  (Tab)        -> Marker 1 */
    ['\r'] = 1, /* Index 13 (Carriage)   -> Marker 1 */
    ['#']  = 2, /* Index 35 (Hash)       -> Marker 2 */
    ['\n'] = 3  /* Index 10 (Newline)    -> Marker 3 */
    /* All other 251 elements will be automatically initialized to 0 */
};

/**
 * Internal utility to skip whitespace and comments.
 * Advances the parser position until a non-whitespace structural token is found.
 *
 * @param p Pointer to the current parser runtime context.
 */
static inline void toml_skip_whitespace(TomlParser *p) {
    while (p->pos < p->length) {
        uint8_t c = p->src[p->pos];
        uint8_t action = TOML_WHITESPACE_LUT[c];

        if (action == 1 || action == 3) {
            p->pos++;
        } else if (action == 2) {
            while (p->pos < p->length && p->src[p->pos] != '\n') {
                p->pos++;
            }
        } else {
            break;
        }
    }
}

TomlString toml_parse_key(TomlParser* p) {
    toml_skip_whitespace(p);
    const char* start = &p->src[p->pos];
    size_t len = 0;
    while (p->pos < p->length) {
        uint8_t c = p->src[p->pos];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            p->pos++;
            len++;
        } else {
            break;
        }
    }
    TomlString rt = {start, len};
    return rt;
}


static inline TomlString toml_parse_string_value_scalar(TomlParser* p) {
    if (p->src[p->pos] == '"') p->pos++;
    TomlString val = { .ptr = &p->src[p->pos], .len = 0 };
    size_t start_content = p->pos;

    while (p->pos < p->length) {
        if (p->src[p->pos] == '"' && (p->pos == start_content || p->src[p->pos - 1] != '\\')) {
            p->pos++;
            break;
        } else {
            p->pos++;
            val.len++;
        }
    }

    return val;
}


int toml_parse(TomlParser* p) {
    while (p->pos < p->length) {
        toml_skip_whitespace(p);
        if (p->pos >= p->length) break;

        char c = p->src[p->pos];
        if (c == '[') {
            p->pos++;
            TomlString table_name = toml_parse_key(p);
            toml_skip_whitespace(p); // Skip something like this: [owner ]
            if (p->src[p->pos] == ']') p->pos++; // Skipping ]

            TomlNode *node = &p->nodes[p->node_count++];
            node->type = TOML_NODE_TABLE;
            node->key = table_name;

            continue;
        } else {
            TomlString key = toml_parse_key(p);
            TomlString val = { .ptr = NULL, .len = 0 };

            if (key.len == 0) { // Skip corrupted symbols
                p->pos++; continue;
            }
            toml_skip_whitespace(p);
            if (p->src[p->pos] == '=') p->pos++;
            toml_skip_whitespace(p);
            if (p->src[p->pos] == '"') {
                val = toml_parse_string_value_scalar(p);
            } else {
                // Numbers, booleans and plain text
                val.ptr = &p->src[p->pos]; // Remember the start of the number

                while (p->pos < p->length &&
                    p->src[p->pos] != '\n' &&
                    p->src[p->pos] != '\r' &&
                    p->src[p->pos] != '#') {
                    p->pos++;
                val.len++;
                }
            }

            TomlNode *node = &p->nodes[p->node_count++];
            node->type = TOML_NODE_KEY_VALUE;
            node->key = key;
            node->val = val;
        }
    }
    return 0;
}

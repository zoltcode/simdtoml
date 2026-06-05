#include "simdtoml.h"

#include <immintrin.h> // For SIMD and AVX2 optimizations


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


/**
 * Advanced AVX2-accelerated parser subroutine to locate a closing quote character.
 * Scans memory fields in bulk blocks of 32 bytes per single CPU clock cycle.
 */
static inline SimdResult toml_find_quote_avx2(const char *src, size_t start_pos, size_t length) {
    size_t pos = start_pos;

    /*
     * Broadcast the target character '"' (ASCII 0x22) across all 32 bytes
     * of a 256-bit SIMD vector register block.
     * Name anatomy: _mm256 (AVX2 vector) + set1 (replicate value) + epi8 (signed 8-bit bytes).
     */
    __m256i target = _mm256_set1_epi8('"');

    /*
     * Vectorized Pipeline Loop: safe to execute as long as
     * at least a full 32-byte memory chunk remains available.
     */
    while (pos + 32 <= length) {

        /*
         * Perform an unaligned memory fetch of 32 bytes from the text stream buffer.
         * Name anatomy: _mm256 + loadu (unaligned memory access) + si256 (generic 256-bit int register).
         */
        __m256i chunk = _mm256_loadu_si256((const __m256i*)(src + pos));

        /*
         * Compare our 32-byte text chunk with the 32-byte target quote register.
         * If byte matches: fills that slot with 0xFF (all bits 1). If not: fills with 0x00.
         * Name anatomy: _mm256 + cmpeq (compare equal) + epi8 (operating on single bytes).
         */
        __m256i cmp_result = _mm256_cmpeq_epi8(chunk, target);

        /*
         * Extract the highest bit of each of the 32 bytes in cmp_result,
         * compacting the heavy 256-bit vector into a lightweight 32-bit scalar integer mask.
         * Name anatomy: _mm256 + movemask (move byte-masks to scalar) + epi8 (on byte targets).
         */
        uint32_t mask = _mm256_movemask_epi8(cmp_result);

        /* If mask is not zero, at least one quote character was found inside this 32-byte chunk */
        if (mask != 0) {

            /*
             * __builtin_ctz (Count Trailing Zeros) is a hardware CPU instruction.
             * It counts the number of trailing zero bits from right to left,
             * instantly revealing the exact byte index of our found quote.
             */
            int index = __builtin_ctz(mask);

            /* Verification fallback check: confirm that the quote character is not escaped via '\' */
            if (pos + index > start_pos && src[pos + index - 1] == '\\') {

                /*
                 * Clear the current lowest active bit in the bitmask sequence
                 * to bypass the escaped quote and check if another quote exists in this same mask.
                 */
                mask &= (mask - 1);
                if (mask != 0) {
                    index = __builtin_ctz(mask);
                    return (SimdResult){ .found_pos = pos + index, .found = 1 };
                }
            } else {
                /* Target structural unescaped quote identified successfully */
                return (SimdResult){ .found_pos = pos + index, .found = 1 };
            }
        }

        /* High-speed leap: advance pointer by 32 bytes at once, skipping 32 iterations */
        pos += 32;
    }

    /*
     * Tail Cleanup Boundary Phase:
     * Process remaining bytes (less than 32) using standard scalar processing loops.
     */
    while (pos < length) {
        if (src[pos] == '"' && src[pos - 1] != '\\') {
            return (SimdResult){ .found_pos = pos, .found = 1 };
        }
        pos++;
    }

    /* Out of bounds tracking boundary error fallback: terminating quote was never found */
    return (SimdResult){ .found_pos = length, .found = 0 };
}


static inline TomlString toml_parse_string_value_simd(TomlParser* p) {
    if (p->src[p->pos] == '"') p->pos++;
    TomlString val = { .ptr = &p->src[p->pos], .len = 0 };
    size_t start_pos = p->pos;

    SimdResult res = toml_find_quote_avx2(p->src, p->pos, p->length);

    if (res.found == 1) {
        val.len = res.found_pos - start_pos;
        p->pos = res.found_pos + 1; /* Advance right past the closing quote */
    } else {
        /* Malformed syntax fallback: terminal quote was never closed inside the file.
         * Consume everything until the end of the buffer to prevent inf-loops. */
        val.len = p->length - start_pos;
        p->pos = p->length;
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
#ifndef NOSIMD
                val = toml_parse_string_value_simd(p);
#else
                val = toml_parse_string_value_scalar(p);
#endif
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

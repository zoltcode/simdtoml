#include "simdtoml.h"

#include <immintrin.h> // For SIMD and AVX2 optimizations
#include <nmmintrin.h> // For SSE4.2 string intrinsics


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

static inline void toml_skip_whitespace_simd(TomlParser *p) {
    // SCALAR FAST-PATH: If the current character is NOT whitespace/comment,
    // exit immediately. Zero cost, prevents SIMD register setup overhead.
    if (p->pos < p->length) {
        uint8_t first_c = (uint8_t)p->src[p->pos];
        if (TOML_WHITESPACE_LUT[first_c] == 0) {
            return;
        }
    }


    // Broadcast all 4 target whitespace characters into 256-bit registers
    __m256i v_space = _mm256_set1_epi8(' ');
    __m256i v_tab   = _mm256_set1_epi8('\t');
    __m256i v_cr    = _mm256_set1_epi8('\r');
    __m256i v_lf    = _mm256_set1_epi8('\n');

    while (p->pos + 32 <= p->length) {
        // Load 32 bytes of text
        __m256i chunk = _mm256_loadu_si256((const __m256i*)(p->src + p->pos));

        // Compare chunk against each whitespace character
        __m256i eq_space = _mm256_cmpeq_epi8(chunk, v_space);
        __m256i eq_tab   = _mm256_cmpeq_epi8(chunk, v_tab);
        __m256i eq_cr    = _mm256_cmpeq_epi8(chunk, v_cr);
        __m256i eq_lf    = _mm256_cmpeq_epi8(chunk, v_lf);

        // Combine all whitespace matches into a single mask vector
        __m256i is_whitespace = _mm256_or_si256(_mm256_or_si256(eq_space, eq_tab),
                                                _mm256_or_si256(eq_cr, eq_lf));

        // Move to 32-bit scalar mask
        uint32_t ws_mask = _mm256_movemask_epi8(is_whitespace);

        // Invert the mask: 1s now represent non-whitespace characters
        uint32_t non_ws_mask = ~ws_mask;

        if (non_ws_mask != 0) {
            // Found a non-whitespace character within these 32 bytes
            int index = __builtin_ctz(non_ws_mask);
            p->pos += index;

            // If it's a comment, handle it via scalar logic and continue skipping
            if (p->src[p->pos] == '#') {
                while (p->pos < p->length && p->src[p->pos] != '\n') {
                    p->pos++;
                }
                continue;
            }
            return; // Hit a structural token (like '[', '=', '"'), parsing must resume
        }

        // Entire 32-byte block is whitespace, skip it instantly
        p->pos += 32;
    }

    // Scalar fallback tail cleanup for the remaining trailing bytes (< 32)
    while (p->pos < p->length) {
        uint8_t c = (uint8_t)p->src[p->pos];
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
         * Compare 32-byte text chunk with the 32-byte target quote register.
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
             * instantly revealing the exact byte index of found quote.
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


static inline size_t toml_find_structural_sse42(const char *src, size_t start_pos, size_t length) {
    size_t pos = start_pos;

    // Define 6 structural target delimiter tokens (padded with zeros to 16 bytes)
    __m128i target_chars = _mm_setr_epi8('=', '[', ']', '\n', '#', '"', 0,0,0,0,0,0,0,0,0,0);

    while (pos + 16 <= length) {
        // Load a 16-byte chunk of text
        __m128i chunk = _mm_loadu_si128((const __m128i*)(src + pos));

        // _SIDD_UBYTE_OPS: unsigned 8-bit bytes
        // _SIDD_CMP_EQUAL_ANY: check if any byte in chunk matches any byte in target_chars
        int index = _mm_cmpistri(target_chars, chunk, _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY);

        // If index < 16, a delimiter was found inside this 16-byte block
        if (index < 16) {
            return pos + index;
        }
        pos += 16; // Blazing fast 16-byte skip
    }

    // Tail cleanup for the remaining trailing bytes (< 16)
    while (pos < length) {
        char c = src[pos];
        if (c == '=' || c == '[' || c == ']' || c == '\n' || c == '#' || c == '"') {
            return pos;
        }
        pos++;
    }
    return length;
}





#ifndef NOSIMD
#define SKIP_WHITESPACE(p) toml_skip_whitespace_simd(p)
#else
#define SKIP_WHITESPACE(p) toml_skip_whitespace(p);
#endif


#ifndef NOSIMD
/* High-speed hardware-accelerated 16-byte vector search */
#define FIND_STRUCTURAL(src, pos, len) toml_find_structural_sse42(src, pos, len)
#else
/**
 * Pure scalar fallback implementation of structural indexing.
 * Identical token alignment on platforms without SSE4.2 (e.g., ARM, Embedded).
 */
static inline size_t toml_find_structural_scalar(const char *src, size_t start_pos, size_t length) {
    size_t pos = start_pos;
    while (pos < length) {
        char c = src[pos];
        if (c == '=' || c == '[' || c == ']' || c == '\n' || c == '#' || c == '"') {
            return pos;
        }
        pos++;
    }
    return length;
}
#define FIND_STRUCTURAL(src, pos, len) toml_find_structural_scalar(src, pos, len)
#endif

int toml_parse(TomlParser* p) {
    while (p->pos < p->length) {
        // Fast forward natively directly to the next structural anchor token delimiter
        size_t next_anchor = FIND_STRUCTURAL(p->src, p->pos, p->length);
        if (next_anchor >= p->length) break;

        char anchor_char = p->src[next_anchor];

        // Case A: Table Block Boundary Header Discovery: [table_name]
        if (anchor_char == '[') {
            p->pos = next_anchor + 1; // Skip opening bracket '['

            // Vectorized leap to locate the corresponding closing bracket
            size_t close_bracket = FIND_STRUCTURAL(p->src, p->pos, p->length);
            if (close_bracket >= p->length || p->src[close_bracket] != ']') {
                p->pos = p->length;
                break; // Critical syntax corruption boundary fallback
            }

            // Zero-Copy Assignment: capture bounds with mathematical address mapping
            TomlNode *node = &p->nodes[p->node_count++];
            node->type = TOML_NODE_TABLE;
            node->key.ptr = &p->src[p->pos];
            node->key.len = close_bracket - p->pos; // Instant slice width computation
            node->val = (TomlString){ .ptr = NULL, .len = 0 };

            p->pos = close_bracket + 1; // Move past ']'
            continue;
        }

        // Case B: Property Mapping Declaration Operator: key = value
        if (anchor_char == '=') {
            // Everything encapsulated between previous step index and this '=' bounds the key
            TomlString key = { .ptr = &p->src[p->pos], .len = next_anchor - p->pos };

            // Inline scalar trimming for key string slice flags
            while (key.len > 0 && (key.ptr[key.len - 1] == ' ' || key.ptr[key.len - 1] == '\t')) {
                key.len--;
            }
            while (key.len > 0 && (*key.ptr == ' ' || *key.ptr == '\t' || *key.ptr == '\n' || *key.ptr == '\r')) {
                key.ptr++;
                key.len--;
            }

            p->pos = next_anchor + 1; // Move right past assignment operator '='

            // Scan forward to locate whether a string quote block or raw scalar primitive follows
            size_t val_anchor = FIND_STRUCTURAL(p->src, p->pos, p->length);
            if (val_anchor >= p->length) break;

            TomlString val = { .ptr = NULL, .len = 0 };

            if (p->src[val_anchor] == '"') {
                p->pos = val_anchor;
                #ifndef NOSIMD
                val = toml_parse_string_value_simd(p); // Accelerated AVX2 vector routine
                #else
                val = toml_parse_string_value_scalar(p); // Standard escape character loop
                #endif
            } else {
                // Pure non-quoted value: numbers, booleans or literal bare text tokens
                val.ptr = &p->src[p->pos];
                while (val.ptr < &p->src[val_anchor] && (*val.ptr == ' ' || *val.ptr == '\t')) {
                    val.ptr++; // Strip leading whitespaces
                }
                val.len = val_anchor - (val.ptr - p->src);
            }

            // Push verified valid mapping sequences directly into the sequential Flat AST memory stack
            if (key.len > 0) {
                TomlNode *node = &p->nodes[p->node_count++];
                node->type = TOML_NODE_KEY_VALUE;
                node->key = key;
                node->val = val;
            }

            p->pos = val_anchor;
            continue;
        }

        // Fallback: Bypassing linebreaks, comments or unmapped text sequences
        p->pos = next_anchor + 1;
    }
    return 0;
}

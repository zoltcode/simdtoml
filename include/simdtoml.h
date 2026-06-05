#ifndef SIMDTOML_H
#define SIMDTOML_H

#include <stdint.h>
#include <stddef.h>

/**
 * Types of nodes supported by our flat AST.
 */
typedef enum {
    TOML_NODE_NONE = 0,
    TOML_NODE_TABLE,     /* Represents a [table] section */
    TOML_NODE_KEY_VALUE  /* Represents a key = value pair */
} TomlNodeType;

/**
 * A lightweight, non-owning string view (Zero-Copy representation).
 * Does not allocate or copy data, just points to the original buffer.
 */
typedef struct {
    const char *ptr;     /* Pointer to the start of the string in the source buffer */
    size_t len;          /* Length of the string segment */
} TomlString;

/**
 * A flat AST node representing an element inside the TOML file.
 * Kept sequential in memory to prevent cache misses.
 */
typedef struct {
    TomlNodeType type;   /* Node type discriminator */
    TomlString key;      /* Table name or Key identifier */
    TomlString val;      /* Value string (only valid if type is TOML_NODE_KEY_VALUE) */
} TomlNode;

/**
 * Parser runtime context holding the processing state.
 */
typedef struct {
    const char *src;     /* Pointer to the raw input string buffer */
    size_t pos;          /* Current byte offset inside the source buffer */
    size_t length;       /* Total size of the input buffer in bytes */
    TomlNode *nodes;     /* Pre-allocated flat array acting as our AST memory */
    size_t node_count;   /* Total number of successfully parsed nodes */
} TomlParser;

typedef struct {
    size_t found_pos;
    int found;
} SimdResult;

/**
 * Main entry point for the TOML parser pipeline.
 * Parses the source buffer and populates the flat node array.
 *
 * @param p Pre-initialized parser context.
 * @return 0 on success, non-zero on syntax or boundary errors.
 */
int toml_parse(TomlParser *p);

#endif /* SIMDTOML_H */

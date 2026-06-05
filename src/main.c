#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "simdtoml.h"

int main() {
    /*
     * Complex TOML string payload for verification.
     * Contains tables, strings, numeric primitives, booleans, and comments.
     */
    const char *toml_data =
    "# Global Configuration Metadata Component\n"
    "[owner]\n"
    "name = \"Tom Preston-Werner\"\n"
    "id = 9942                  # Inline comment example\n"
    "active = true\n\n"
    "[database]\n"
    "server = \"192.168.1.1\"\n"
    "ports = 8001\n"
    "connections = 128";

    size_t len = strlen(toml_data);

    /*
     * Pre-allocate Flat AST sequence memory blocks upfront.
     * Memory requirements scale linear to content footprint size.
     */
    TomlNode *nodes_array = malloc(sizeof(TomlNode) * len);
    if (!nodes_array) {
        fprintf(stderr, "Fatal: Flat AST node memory allocation failed\n");
        return -1;
    }

    /* Initialize runtime parsing pipeline wrapper */
    TomlParser parser = {
        .src = toml_data,
        .pos = 0,
        .length = len,
        .nodes = nodes_array,
        .node_count = 0
    };

    /* Execute the parser algorithm engine */
    toml_parse(&parser);

    /* Process compiled flat node metadata block sequence logs */
    printf("=== SIMDTOML SCALAR ENGINE VALIDATION REPORT ===\n");
    printf("Total successfully extracted nodes: %zu\n", parser.node_count);

    for (size_t i = 0; i < parser.node_count; i++) {
        TomlNode n = parser.nodes[i];

        if (n.type == TOML_NODE_TABLE) {
            printf("\n[Table Header] Target: %.*s\n", (int)n.key.len, n.key.ptr);
        }
        else if (n.type == TOML_NODE_KEY_VALUE) {
            /*
             * Safety fallback fallback display check.
             * If pointer is empty, map output value tag to literal string token identifier.
             */
            if (n.val.ptr != NULL) {
                printf("  [Property Map] Key: %-12.*s -> Value: %.*s\n",
                       (int)n.key.len, n.key.ptr, (int)n.val.len, n.val.ptr);
            } else {
                printf("  [Property Map] Key: %-12.*s -> Value: [EMPTY / NULL]\n",
                       (int)n.key.len, n.key.ptr);
            }
        }
    }

    printf("\n=== VALIDATION RECOVERY COMPLETED ===\n");

    /* Free heap resources */
    free(nodes_array);
    return 0;
}

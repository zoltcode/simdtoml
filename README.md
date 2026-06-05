# simdtoml

A high-performance, **Zero-Allocation**, **Flat AST** TOML parsing engine written in pure C11, accelerated by **SSE4.2 and AVX2 SIMD** hardware intrinsics.

## 🚀 Performance Metrics

Evaluated on Fedora Linux (GCC 15, Release build configurations via `-O3 -mavx2 -msse4.2`):

```text
=== SIMDTOML PERFORMANCE BENCHMARK ENGINE ===
Loaded target file: large_benchmark.toml (4550000 bytes / 4.34 MB)

Total iterations run         : 100
Extracted nodes per run      : 250000
Average execution latency    : 4.4506 ms
Total cumulative process time: 445.06 ms
Calculated raw processing speed: **0.952 GB/s**
```

## 🛠️ Key Architectural Advantages

* **Zero-Allocation (Zero-Copy)**: The parser never pauses to claim new memory (`malloc`) while reading the file. Instead of making new copies of keys and values, it simply remembers where they start and end in the original text.
* **Flat Memory Layout**: Traditional parsers build a messy "tree" of data connected by pointers, forcing the processor to jump all over your RAM. This library loads all items one after another in a single flat array, allowing the CPU to read data sequentially at maximum hardware speeds.
* **Structural Leapfrogging (SSE4.2)**: Instead of crawling through the file character-by-character to find text keys, the engine uses 16-byte vector sweeps. It instantly leaps past spaces and text, stopping only when it hits structural anchors like `=` or `[`.
* **Bulk String Scanning (AVX2)**: When the parser encounters a quoted text value, it switches to ultra-wide 32-byte scanning blocks. This allows it to find the closing quote and calculate the string's length in massive, high-speed jumps.


## 📦 Setup & Compilation

Configure the build layout directory and compile the binary target using CMake:

```bash
# 1. Configure release flags natively
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 2. Compile target static library and test benchmark binary
cmake --build build
```

To execute the embedded benchmark suite, generate the multi-megabyte test file and run the binary execution block:

```bash
# Generate a dense ~4.34 MB TOML file from the official spec test suites
curl -sSL https://raw.githubusercontent.com/toml-lang/toml-test/af5f8052e9109206ad3977508263c97907f0797d/tests/valid/example.toml -o sample.toml
for i in {1..50000}; do cat sample.toml >> build/large_benchmark.toml; done

# Execute performance tracking run
cd build && ./main
```

## 💻 API Integration Example

Integrating `simdtoml` is done like this:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "simdtoml.h"

int main() {
    const char *toml_payload = "[server]\nhost = \"127.0.0.1\"\nport = 8080";
    size_t length = strlen(toml_payload);

    // Allocate memory for the linear node stream sequence once upfront
    TomlNode *ast_pool = malloc(sizeof(TomlNode) * length);

    // Initialize the context tracker bound parameters
    TomlParser parser = {
        .src = toml_payload,
        .pos = 0,
        .length = length,
        .nodes = ast_pool,
        .node_count = 0
    };

    // Execute the parser algorithm engine
    if (toml_parse(&parser) == 0) {
        printf("Successfully extracted %zu flat nodes.\n", parser.node_count);
        
        for (size_t i = 0; i < parser.node_count; i++) {
            if (parser.nodes[i].type == TOML_NODE_KEY_VALUE) {
                printf("Property Key: %.*s -> Value: %.*s\n", 
                       (int)parser.nodes[i].key.len, parser.nodes[i].key.ptr,
                       (int)parser.nodes[i].val.len, parser.nodes[i].val.ptr);
            }
        }
    }

    free(ast_pool);
    return 0;
}
```

## 📜 License

This project is licensed under the MIT License - see the LICENSE.txt file for details.

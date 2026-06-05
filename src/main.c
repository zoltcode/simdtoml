#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "simdtoml.h"

/* Helper to convert timespec struct into fractional milliseconds */
static inline double get_elapsed_ms(struct timespec start, struct timespec end) {
    return (double)(end.tv_sec - start.tv_sec) * 1000.0 +
    (double)(end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main() {
    const char *filename = "large_benchmark.toml";

    /* Open and inspect file descriptor blocks */
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Fatal Error: Cannot open file '%s'. Run the terminal generation script first.\n", filename);
        return -1;
    }

    /* Calculate total payload memory footprint size bytes */
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Allocate buffer and stream binary contents directly into RAM */
    char *file_buffer = malloc(file_size + 1);
    if (!file_buffer) {
        fclose(f);
        return -1;
    }
    size_t read_bytes = fread(file_buffer, 1, file_size, f);
    file_buffer[read_bytes] = '\0';
    fclose(f);

    printf("=== SIMDTOML PERFORMANCE BENCHMARK ENGINE ===\n");
    printf("Loaded target file: %s (%zu bytes / %.2f MB)\n\n",
           filename, file_size, (double)file_size / (1024.0 * 1024.0));

    /* Pre-allocate Flat AST sequence token slots block metadata sequence array once */
    TomlNode *nodes_array = malloc(sizeof(TomlNode) * file_size);
    if (!nodes_array) {
        free(file_buffer);
        return -1;
    }

    /*
     * Iterations block run sequence loops.
     * We execute the full pipeline multiple times to warm up CPU cache pipelines
     * and get an accurate average speed reading.
     */
    const int iterations = 100;
    size_t total_nodes_extracted = 0;

    struct timespec start_time, end_time;

    /* Benchmark Start Point Anchor */
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    for (int i = 0; i < iterations; i++) {
        /* Reset parser context constraints variables on every loop sequence step */
        TomlParser parser = {
            .src = file_buffer,
            .pos = 0,
            .length = file_size,
            .nodes = nodes_array,
            .node_count = 0
        };

        toml_parse(&parser);
        total_nodes_extracted = parser.node_count; /* Retain for validation telemetry */
    }

    /* Benchmark End Point Anchor */
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    /* Metric math summaries computation analytics */
    double total_ms = get_elapsed_ms(start_time, end_time);
    double avg_ms = total_ms / (double)iterations;

    /* Throughput capacity math conversion: Bytes processed per unit of time */
    double processed_gb = ((double)file_size * (double)iterations) / (1024.0 * 1024.0 * 1024.0);
    double duration_seconds = total_ms / 1000.0;
    double throughput_gb_s = processed_gb / duration_seconds;

    /* Telemetry Report Terminal Log Output */
    printf("Benchmark Profile Analytics Metrics Completed:\n");
    printf("--------------------------------------------------\n");
    printf("Total iterations run         : %d\n", iterations);
    printf("Extracted nodes per run      : %zu\n", total_nodes_extracted);
    printf("Average execution latency    : %.4f ms\n", avg_ms);
    printf("Total cumulative process time: %.2f ms\n", total_ms);
    printf("Calculated raw processing speed: **%.3f GB/s**\n", throughput_gb_s);
    printf("--------------------------------------------------\n");

    /* Cleanup all operational context components */
    free(nodes_array);
    free(file_buffer);
    return 0;
}

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <xxh3.h>
#include <mimalloc-override.h>

#define LIMIT 512

static inline uint32_t convert_hash_to_index(uint64_t hashCode, uint32_t size) {
    return (uint32_t)hashCode & (size - 1);
}

static void calculateCollisionCounts(uint32_t limit, uint64_t *hashCodes, uint32_t dataCount) {
    uint32_t* table = (uint32_t *)malloc(limit * sizeof(uint32_t));
    if (!table) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }

    printf("\nKey Count:%d \nCollision counts for different table sizes:\n",dataCount);
    uint32_t size = 32;
    while (dataCount > size) size <<= 1;

    for (; size <= limit; size <<= 1) {
        memset(table, 0, size * sizeof(uint32_t));

        uint32_t collisions = 0;
        uint32_t maxProbe = 0;
        uint32_t index = 0;
        uint32_t probe = 0;

        for (uint32_t i = 0; i < dataCount; ++i) {
            index = convert_hash_to_index(hashCodes[i],size); // better then % faster and uniform
            probe = 0;

            while (table[index] != 0) {
                collisions++;
                probe++;

                if(++index == size) index = 0;

                if (probe >= size) {
                    fprintf(stderr, "Error: Hash table is full (size = %u, dataCount = %u)\n", size, dataCount);
                    exit(1);
                }
            }

            maxProbe = maxProbe < probe ? probe : maxProbe;
            table[index] = 1;
        }
        printf("Table size: %u, Collision: %u, Biggist Probe: %u %s\n\n", size, collisions,maxProbe, collisions ? "" : "Good");
        if (!collisions) break;//because it is good!
    }

    free(table);
}

int main() {
	printf("MI_MALLOC VER %d \n", mi_version());

    char inputLine[4096];
    uint32_t maxStrings = 1000;
    uint64_t *hashCodes = (uint64_t*)malloc(maxStrings * sizeof(uint64_t));
    if (!hashCodes) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }

    uint32_t dataCount = 0;

    printf("Enter a long string of words separated by spaces (max 4096 characters):\n");

    if (fgets(inputLine, sizeof(inputLine), stdin) == NULL) {
        printf("No input provided. Exiting.\n");
        free(hashCodes);
        return 0;
    }

    inputLine[strcspn(inputLine, "\n")] = '\0';

    char *token = strtok(inputLine, " ");
    while (token != NULL && dataCount < maxStrings) {
        uint64_t hash = XXH3_64bits(token,strlen(token));
        hashCodes[dataCount] = hash;

        printf("String: %-20s, Hash: %#10x\n", token, hash);

        dataCount++;
        token = strtok(NULL, " ");
    }

    if (dataCount == 0) {
        printf("No valid strings found. Exiting.\n");
        free(hashCodes);
        return 0;
    }

    calculateCollisionCounts(LIMIT, hashCodes, dataCount);

    free(hashCodes);

    mi_stats_print(NULL);
    return 0;
}


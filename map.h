#ifndef MAP_H
#define MAP_H

#include "game.h"

/*
 * Parse a map file into *map.
 * Allocates map->cells and map->bonus_types on the heap.
 * Call map_free() when done.
 * Returns 0 on success, non-zero on error (prints reason to stderr).
 */
int  parse_map(const char *path, Map *map);

/* Free heap memory owned by map. Safe to call on a zero-initialized Map. */
void map_free(Map *map);

/*
 * Generate a random map into *map.
 * width/height are rounded up to the nearest odd number (minimum 7).
 * Retries internally until all player spawns are connected via BFS.
 */
int map_generate(Map *map, uint8_t width, uint8_t height,
                 uint8_t player_count, unsigned int seed, GameMode mode);

#endif /* MAP_H */
#include "map.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int parse_map(const char *path, Map *map)
{
    memset(map, 0, sizeof(*map));

    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "map: cannot open '%s'\n", path);
        return -1;
    }

    /* Line 1: width height */
    unsigned width = 0, height = 0;
    if (fscanf(file, "%u %u\n", &width, &height) != 2 ||
        width == 0 || height == 0 ||
        width > MAX_MAP_WIDTH || height > MAX_MAP_HEIGHT) {
        fprintf(stderr, "map: invalid dimensions %u x %u\n", width, height);
        fclose(file);
        return -1;
    }
    map->width  = (uint8_t)width;
    map->height = (uint8_t)height;

    /* Line 2: explosion duration in ticks */
    unsigned duration = 0;
    if (fscanf(file, "%u\n", &duration) != 1 || duration == 0) {
        fprintf(stderr, "map: invalid explosion duration %u\n", duration);
        fclose(file);
        return -1;
    }
    map->explosion_duration = (uint16_t)(duration > 65535 ? 65535 : duration);

    /* Allocate grid arrays */
    size_t num_cells = (size_t)width * height;
    map->cells       = calloc(num_cells, sizeof(CellType));
    map->bonus_types = calloc(num_cells, sizeof(BonusType));
    if (!map->cells || !map->bonus_types) {
        fprintf(stderr, "map: out of memory\n");
        map_free(map);
        fclose(file);
        return -1;
    }

    /* Track which spawn ids were found */
    bool found_spawn[MAX_PLAYERS] = {false};

    /* Lines 3+: grid rows */
    char line[MAX_MAP_WIDTH + 4]; /* +4 for \r\n\0 and safety */
    for (uint8_t y = 0; y < map->height; y++) {
        if (!fgets(line, (int)sizeof(line), file)) {
            /* Fewer rows than declared — remaining cells stay EMPTY (calloc) */
            break;
        }
        for (uint8_t x = 0; x < map->width; x++) {
            char c = line[x];
            if (c == '\0' || c == '\n' || c == '\r') break;

            size_t idx = (size_t)y * map->width + x;
            switch (c) {
            case 'H':
                map->cells[idx] = CELL_HARD_WALL;
                break;
            case 'S':
                map->cells[idx] = CELL_SOFT_BLOCK;
                break;
            case 'X':
                map->cells[idx] = CELL_BONUS;
                map->bonus_types[idx] = BONUS_SPEED;
                break;
            case 'Y':
                map->cells[idx] = CELL_BONUS;
                map->bonus_types[idx] = BONUS_RADIUS;
                break;
            case 'Z':
                map->cells[idx] = CELL_BONUS;
                map->bonus_types[idx] = BONUS_FUSE;
                break;
            case 'A':
                map->cells[idx] = CELL_BONUS;
                map->bonus_types[idx] = BONUS_SHIELD;
                break;
            case 'B':
                map->cells[idx] = CELL_BONUS;
                map->bonus_types[idx] = BONUS_KICK;
                break;
            case 'C':
                map->cells[idx] = CELL_BONUS;
                map->bonus_types[idx] = BONUS_RAPID;
                break;
            case '.':
                map->cells[idx] = CELL_EMPTY;
                break;
            default:
                if (c >= '1' && c <= '8') {
                    uint8_t pid = (uint8_t)(c - '1');
                    map->cells[idx] = CELL_EMPTY;
                    map->spawn_x[pid] = x;
                    map->spawn_y[pid] = y;
                    found_spawn[pid]  = true;
                }
                /* Unknown chars treated as EMPTY */
                break;
            }
        }
    }

    fclose(file);

    /* Count consecutive spawn ids starting at 0 */
    map->player_count = 0;
    for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
        if (found_spawn[i]) map->player_count = i + 1;
    }

    return 0;
}

void map_free(Map *map)
{
    if (!map) return;
    free(map->cells);
    free(map->bonus_types);
    map->cells       = NULL;
    map->bonus_types = NULL;
}

/* ── BFS connectivity check ─────────────────────────────────────────────── */

static bool bfs_spawns_connected(const Map *map, uint8_t player_count)
{
    size_t ncells = (size_t)map->width * map->height;
    bool *visited = calloc(ncells, sizeof(bool));
    int  *queue   = malloc(ncells * sizeof(int));
    if (!visited || !queue) { free(visited); free(queue); return true; }

    int head = 0, tail = 0;
    int start = map->spawn_y[0] * map->width + map->spawn_x[0];
    visited[start] = true;
    queue[tail++]  = start;

    const int dx[4] = {0, 0, -1, 1};
    const int dy[4] = {-1, 1, 0, 0};

    while (head < tail) {
        int cell_idx = queue[head++];
        int x = cell_idx % map->width;
        int y = cell_idx / map->width;
        for (int dir = 0; dir < 4; dir++) {
            int nx = x + dx[dir], ny = y + dy[dir];
            if (nx < 0 || nx >= map->width || ny < 0 || ny >= map->height) continue;
            int neighbor_idx = ny * map->width + nx;
            if (visited[neighbor_idx]) continue;
            if (map->cells[neighbor_idx] != CELL_EMPTY) continue;
            visited[neighbor_idx] = true;
            queue[tail++] = neighbor_idx;
        }
    }

    bool all_reachable = true;
    for (int player_idx = 1; player_idx < player_count; player_idx++) {
        int spawn_idx = map->spawn_y[player_idx] * map->width + map->spawn_x[player_idx];
        if (!visited[spawn_idx]) { all_reachable = false; break; }
    }

    free(visited);
    free(queue);
    return all_reachable;
}

/* ── Random map generator ───────────────────────────────────────────────── */

/* Spawn positions for up to 8 players (indices match player ids) */
static void set_spawns(Map *map)
{
    uint8_t width = map->width, height = map->height;
    map->spawn_x[0] = 1;         map->spawn_y[0] = 1;
    map->spawn_x[1] = width - 2; map->spawn_y[1] = height - 2;
    map->spawn_x[2] = width - 2; map->spawn_y[2] = 1;
    map->spawn_x[3] = 1;         map->spawn_y[3] = height - 2;
    map->spawn_x[4] = 1;         map->spawn_y[4] = height / 2;
    map->spawn_x[5] = width - 2; map->spawn_y[5] = height / 2;
    map->spawn_x[6] = width / 2; map->spawn_y[6] = 1;
    map->spawn_x[7] = width / 2; map->spawn_y[7] = height - 2;
}

int map_generate(Map *map, uint8_t width, uint8_t height,
                 uint8_t player_count, unsigned int seed, GameMode mode)
{
    /* Enforce odd dimensions (checkerboard requires it), minimum 7 */
    if (width  < 7) width  = 7;
    if (height < 7) height = 7;
    if (!(width  & 1)) width++;
    if (!(height & 1)) height++;
    if (player_count == 0) player_count = 2;
    if (player_count > MAX_PLAYERS) player_count = MAX_PLAYERS;

    memset(map, 0, sizeof(*map));
    map->width              = width;
    map->height             = height;
    map->explosion_duration = 10;
    map->player_count       = player_count;
    set_spawns(map);

    size_t ncells = (size_t)width * height;
    map->cells       = calloc(ncells, sizeof(CellType));
    map->bonus_types = calloc(ncells, sizeof(BonusType));
    if (!map->cells || !map->bonus_types) { map_free(map); return -1; }

    static const BonusType POOL_MOBILITY[] = { BONUS_SPEED, BONUS_KICK,  BONUS_SHIELD };
    static const BonusType POOL_BIG_BOOM[] = { BONUS_RAPID, BONUS_KICK,  BONUS_RADIUS };

    const BonusType *bonus_pool;
    int              pool_size;
    if (mode == GAMEMODE_BIG_BOOM) {
        bonus_pool = POOL_BIG_BOOM; pool_size = 3;
    } else {                              /* GAMEMODE_MOBILITY (default) */
        bonus_pool = POOL_MOBILITY; pool_size = 3;
    }

    /* Try up to 100 seeds until all spawns are connected */
    for (int attempt = 0; attempt <= 100; attempt++) {
        srand(seed + (unsigned)attempt);
        memset(map->cells, 0, ncells * sizeof(CellType));

        /* Border */
        for (uint8_t x = 0; x < width; x++) {
            map->cells[x]                         = CELL_HARD_WALL;
            map->cells[(height - 1) * width + x]  = CELL_HARD_WALL;
        }
        for (uint8_t y = 0; y < height; y++) {
            map->cells[y * width]             = CELL_HARD_WALL;
            map->cells[y * width + width - 1] = CELL_HARD_WALL;
        }

        /* Checkerboard pillars (even x AND even y, interior only) */
        for (int y = 2; y < height - 1; y += 2)
            for (int x = 2; x < width - 1; x += 2)
                map->cells[y * width + x] = CELL_HARD_WALL;

        /* Random soft blocks (~80% of eligible empty cells) */
        for (int y = 1; y < height - 1; y++) {
            for (int x = 1; x < width - 1; x++) {
                if (map->cells[y * width + x] != CELL_EMPTY) continue;
                if (rand() % 10 < 8)
                    map->cells[y * width + x] = CELL_SOFT_BLOCK;
            }
        }

        /* Clear spawn areas (spawn cell + 4 neighbours) so players can move */
        const int offsets[5][2] = {{0,0},{1,0},{-1,0},{0,1},{0,-1}};
        for (int player_idx = 0; player_idx < player_count; player_idx++) {
            for (int offset_idx = 0; offset_idx < 5; offset_idx++) {
                int clear_x = map->spawn_x[player_idx] + offsets[offset_idx][0];
                int clear_y = map->spawn_y[player_idx] + offsets[offset_idx][1];
                if (clear_x < 0 || clear_x >= width ||
                    clear_y < 0 || clear_y >= height) continue;
                if (map->cells[clear_y * width + clear_x] == CELL_HARD_WALL) continue;
                map->cells[clear_y * width + clear_x] = CELL_EMPTY;
            }
        }

        /* BFS check — retry if spawns aren't all reachable */
        if (attempt < 100 && !bfs_spawns_connected(map, player_count))
            continue;

        /* Place bonus items (replace ~10% of soft blocks) */
        for (int y = 1; y < height - 1; y++) {
            for (int x = 1; x < width - 1; x++) {
                if (map->cells[y * width + x] != CELL_SOFT_BLOCK) continue;
                if (rand() % 100 < 10) {
                    map->cells[y * width + x] = CELL_BONUS;
                    map->bonus_types[y * width + x] =
                        bonus_pool[rand() % pool_size];
                }
            }
        }
        break;
    }

    return 0;
}

#include "map.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int parse_map(const char *path, Map *map)
{
    memset(map, 0, sizeof(*map));

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "map: cannot open '%s'\n", path);
        return -1;
    }

    /* Line 1: width height */
    unsigned w = 0, h = 0;
    if (fscanf(f, "%u %u\n", &w, &h) != 2 || w == 0 || h == 0 ||
        w > MAX_MAP_WIDTH || h > MAX_MAP_HEIGHT) {
        fprintf(stderr, "map: invalid dimensions %u x %u\n", w, h);
        fclose(f);
        return -1;
    }
    map->width  = (uint8_t)w;
    map->height = (uint8_t)h;

    /* Line 2: explosion duration in ticks */
    unsigned dur = 0;
    if (fscanf(f, "%u\n", &dur) != 1 || dur == 0) {
        fprintf(stderr, "map: invalid explosion duration %u\n", dur);
        fclose(f);
        return -1;
    }
    map->explosion_duration = (uint16_t)(dur > 65535 ? 65535 : dur);

    /* Allocate grid arrays */
    size_t cells = (size_t)w * h;
    map->cells       = calloc(cells, sizeof(CellType));
    map->bonus_types = calloc(cells, sizeof(BonusType));
    if (!map->cells || !map->bonus_types) {
        fprintf(stderr, "map: out of memory\n");
        map_free(map);
        fclose(f);
        return -1;
    }

    /* Track which spawn ids were found */
    bool found_spawn[MAX_PLAYERS] = {false};

    /* Lines 3+: grid rows */
    char line[MAX_MAP_WIDTH + 4]; /* +4 for \r\n\0 and safety */
    for (uint8_t y = 0; y < map->height; y++) {
        if (!fgets(line, (int)sizeof(line), f)) {
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

    fclose(f);

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
        int idx = queue[head++];
        int x = idx % map->width;
        int y = idx / map->width;
        for (int d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d];
            if (nx < 0 || nx >= map->width || ny < 0 || ny >= map->height) continue;
            int nidx = ny * map->width + nx;
            if (visited[nidx]) continue;
            if (map->cells[nidx] != CELL_EMPTY) continue;
            visited[nidx] = true;
            queue[tail++] = nidx;
        }
    }

    bool ok = true;
    for (int p = 1; p < player_count; p++) {
        int sidx = map->spawn_y[p] * map->width + map->spawn_x[p];
        if (!visited[sidx]) { ok = false; break; }
    }

    free(visited);
    free(queue);
    return ok;
}

/* ── Random map generator ───────────────────────────────────────────────── */

/* Spawn positions for up to 8 players (indices match player ids) */
static void set_spawns(Map *map)
{
    uint8_t w = map->width, h = map->height;
    map->spawn_x[0] = 1;     map->spawn_y[0] = 1;
    map->spawn_x[1] = w - 2; map->spawn_y[1] = h - 2;
    map->spawn_x[2] = w - 2; map->spawn_y[2] = 1;
    map->spawn_x[3] = 1;     map->spawn_y[3] = h - 2;
    map->spawn_x[4] = 1;     map->spawn_y[4] = h / 2;
    map->spawn_x[5] = w - 2; map->spawn_y[5] = h / 2;
    map->spawn_x[6] = w / 2; map->spawn_y[6] = 1;
    map->spawn_x[7] = w / 2; map->spawn_y[7] = h - 2;
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
    map->width            = width;
    map->height           = height;
    map->explosion_duration = 10;
    map->player_count     = player_count;
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
            map->cells[x]                      = CELL_HARD_WALL;
            map->cells[(height - 1) * width + x] = CELL_HARD_WALL;
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

        /* Clear spawn areas (spawn cell + 4 neighbours) */
        const int offsets[5][2] = {{0,0},{1,0},{-1,0},{0,1},{0,-1}};
        for (int p = 0; p < player_count; p++) {
            for (int k = 0; k < 5; k++) {
                int cx = map->spawn_x[p] + offsets[k][0];
                int cy = map->spawn_y[p] + offsets[k][1];
                if (cx < 0 || cx >= width || cy < 0 || cy >= height) continue;
                if (map->cells[cy * width + cx] == CELL_HARD_WALL) continue;
                map->cells[cy * width + cx] = CELL_EMPTY;
            }
        }

        /* BFS check — retry if spawns aren't all reachable */
        if (attempt < 100 && !bfs_spawns_connected(map, player_count))
            continue;

        /* Place bonus items (replace ~8% of soft blocks) */
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

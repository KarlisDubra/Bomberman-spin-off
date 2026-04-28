#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>

/* ── Constants ─────────────────────────────────────────────────────────── */

#define TICKS_PER_SECOND    20
#define MAX_PLAYERS          8
#define MAX_BOMBS           64
#define MAX_EXPLOSIONS     256
#define ACTION_QUEUE_SIZE   16   /* must be power-of-two */
#define MAX_MAP_WIDTH      255
#define MAX_MAP_HEIGHT     255
#define RESPAWN_INVINCIBLE_TICKS 60 /* 3s */

#define DEFAULT_LIVES        3
#define DEFAULT_BOMBS        1
#define DEFAULT_RADIUS       1
#define DEFAULT_FUSE_TICKS  60   /* 3 s × 20 ticks/s */
#define DEFAULT_SPEED        3   /* cells/s  →  cooldown = 20/3 = 6 ticks */

#define NO_WINNER           -1
#define GAMEMODE_COUNT       2

/* ── Enums ──────────────────────────────────────────────────────────────── */

typedef enum {
    GAMEMODE_MOBILITY = 0,  /* open arena  — room to manoeuvre */
    GAMEMODE_BIG_BOOM = 1   /* dense arena — close-quarters combat */
} GameMode;

typedef enum {
    CELL_EMPTY     = 0,
    CELL_HARD_WALL = 1,
    CELL_SOFT_BLOCK= 2,
    CELL_BONUS     = 3
} CellType;

typedef enum {
    BONUS_SPEED  = 0,  /* spec default: movement speed +1 */
    BONUS_RADIUS = 1,  /* spec default: bomb radius +1 */
    BONUS_TIMER  = 2,  /* spec default: bomb timer +1 */
    BONUS_SHIELD = 3,  /* special: absorbs one explosion hit */
    BONUS_KICK   = 4,  /* special: moving into a bomb slides it */
    BONUS_MEGA   = 5   /* special: next bomb ignores soft blocks */
} BonusType;

typedef enum {
    ACTION_MOVE_UP    = 0,
    ACTION_MOVE_DOWN  = 1,
    ACTION_MOVE_LEFT  = 2,
    ACTION_MOVE_RIGHT = 3,
    ACTION_PLACE_BOMB = 4
} Action;

/* ── ActionQueue (circular buffer) ─────────────────────────────────────── */

typedef struct {
    Action  buf[ACTION_QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} ActionQueue;

/* ── Player ─────────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  x;
    uint8_t  y;
    uint8_t  speed;             /* cells/s */
    uint8_t  radius;            /* explosion radius in cells */
    uint8_t  bombs_available;
    uint8_t  lives;
    uint8_t  kills;
    uint8_t  blocks_destroyed;
    uint8_t  bonuses_collected;
    uint16_t fuse_time;         /* bomb fuse in ticks */
    uint16_t move_cooldown;     /* ticks until next move allowed */
    uint16_t invincible_ticks;  /* ticks of hit immunity (shield or post-respawn) */
    bool     alive;
    bool     shielded;          /* absorbs next explosion hit */
    bool     can_kick;          /* can slide bombs by walking into them */
    bool     mega_next;         /* next placed bomb will be full-range (mega blast) */
    ActionQueue queue;
} Player;

/* ── Bomb ────────────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  x;
    uint8_t  y;
    uint8_t  owner_id;
    uint8_t  radius;            /* snapshotted from owner at placement time */
    uint16_t ticks_remaining;
    bool     active;            /* false = scheduled for removal */
    bool     full_range;        /* explosion burns through soft blocks to hard wall */
} Bomb;

/* ── ExplosionCell ──────────────────────────────────────────────────────── */

typedef struct {
    uint8_t  x;
    uint8_t  y;
    uint8_t  owner_id;          /* for kill attribution */
    uint16_t ticks_remaining;
} ExplosionCell;

/* ── Map ────────────────────────────────────────────────────────────────── */

typedef struct {
    CellType  *cells;           /* heap, row-major: cells[y*width+x] */
    BonusType *bonus_types;     /* parallel array; valid when cells[i]==CELL_BONUS */
    uint8_t    width;
    uint8_t    height;
    uint8_t    player_count;    /* number of spawn points found in file */
    uint8_t    spawn_x[MAX_PLAYERS];
    uint8_t    spawn_y[MAX_PLAYERS];
    uint16_t   explosion_duration; /* ticks, from map file line 2 */
} Map;

/* ── GameState ──────────────────────────────────────────────────────────── */

typedef struct {
    Map          map;
    Player       players[MAX_PLAYERS];
    uint8_t      player_count;
    Bomb         bombs[MAX_BOMBS];
    uint8_t      bomb_count;
    ExplosionCell explosions[MAX_EXPLOSIONS];
    uint16_t     explosion_count;  /* uint16_t: count up to 256 without overflow */
    uint32_t     tick_count;
    uint32_t     max_ticks;        /* 0 = no time limit */
    bool         game_over;
    int8_t       winner;           /* NO_WINNER(-1) or player id 0..7 */
} GameState;

/* ── Public API ─────────────────────────────────────────────────────────── */

/*
 * NOT THREAD-SAFE.
 * All calls on the same GameState must be serialized by the caller.
 * Use one mutex per GameState when calling from multiple threads.
 */

/* Load map file, allocate and initialize game state. Returns NULL on error. */
GameState *game_init(const char *map_file);

/* Create an empty client-side state that is populated from server messages. */
GameState *game_client_create(void);

/* Replace the client-side map with the MAP payload received from the server. */
int game_client_set_map(GameState *state, uint8_t rows, uint8_t cols,
                        const uint8_t *cells);

/* Apply server-authoritative events to the renderable client mirror state. */
void game_client_set_status(GameState *state, uint8_t player_id,
                            bool alive, bool ready);
void game_client_apply_moved(GameState *state, uint8_t player_id,
                             uint16_t cell_index);
void game_client_apply_bomb(GameState *state, uint8_t player_id,
                            uint16_t cell_index);
void game_client_apply_explosion_start(GameState *state, uint8_t radius,
                                       uint16_t cell_index);
void game_client_apply_explosion_end(GameState *state, uint16_t cell_index);
void game_client_apply_death(GameState *state, uint8_t player_id);
void game_client_apply_bonus_available(GameState *state, uint8_t bonus_type,
                                       uint16_t cell_index);
void game_client_apply_bonus_retrieved(GameState *state, uint8_t player_id,
                                       uint16_t cell_index);
void game_client_apply_block_destroyed(GameState *state, uint16_t cell_index);
void game_client_apply_winner(GameState *state, int8_t winner);

/*
 * Generate a random map and initialize game state.
 * width/height are rounded up to the nearest odd number (minimum 7).
 * Pass time(NULL) as seed for a different map each run.
 */
GameState *game_init_random(uint8_t width, uint8_t height,
                             uint8_t player_count, unsigned int seed,
                             GameMode mode);

/* Advance the game by one tick (call 20 times/second). */
void game_tick(GameState *state);

/*
 * Enqueue an action for a player. Silently drops if:
 *   - player_id >= player_count
 *   - player is dead
 *   - action queue is full
 */
void game_queue_action(GameState *state, uint8_t player_id, Action action);

/* Returns true once a win/draw condition has been reached. */
bool game_is_over(GameState *state);

/* Returns winning player id (0..7), or NO_WINNER (-1) for draw/not-over. */
int  game_get_winner(GameState *state);

/* Free all heap memory owned by state. */
void game_free(GameState *state);

#endif /* GAME_H */

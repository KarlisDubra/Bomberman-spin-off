#include "game.h"
#include "map.h"
#include <stdlib.h>
#include <string.h>

/* ── Action queue helpers ───────────────────────────────────────────────── */

static bool queue_empty(const ActionQueue *q)
{
    return q->count == 0;
}

static void queue_enqueue(ActionQueue *q, Action action)
{
    if (q->count == ACTION_QUEUE_SIZE) return; /* full — drop */
    q->buf[q->tail & (ACTION_QUEUE_SIZE - 1)] = action;
    q->tail++;
    q->count++;
}

static Action queue_dequeue(ActionQueue *q)
{
    Action action = q->buf[q->head & (ACTION_QUEUE_SIZE - 1)];
    q->head++;
    q->count--;
    return action;
}

/* ── Map/bomb lookup helpers ────────────────────────────────────────────── */

static inline CellType cell_at(const GameState *s, uint8_t x, uint8_t y)
{
    return s->map.cells[(size_t)y * s->map.width + x];
}

static inline void set_cell(GameState *s, uint8_t x, uint8_t y, CellType cell_type)
{
    s->map.cells[(size_t)y * s->map.width + x] = cell_type;
}

static inline BonusType bonus_at(const GameState *s, uint8_t x, uint8_t y)
{
    return s->map.bonus_types[(size_t)y * s->map.width + x];
}

/* Returns index into s->bombs[] or -1 if no active bomb at (x,y). */
static int find_bomb_at(const GameState *s, uint8_t x, uint8_t y)
{
    for (int i = 0; i < s->bomb_count; i++) {
        if (s->bombs[i].active && s->bombs[i].x == x && s->bombs[i].y == y)
            return i;
    }
    return -1;
}

/* Returns true if an alive player (other than ignore_player) occupies (x,y).
 * Used to stop a kicked bomb before it slides through another player. */
static bool find_alive_player_at(const GameState *s, uint8_t x, uint8_t y,
                                 int ignore_player)
{
    for (int i = 0; i < s->player_count; i++) {
        if (i == ignore_player) continue;
        if (!s->players[i].alive) continue;
        if (s->players[i].x == x && s->players[i].y == y)
            return true;
    }
    return false;
}

/* ── Explosion helpers ──────────────────────────────────────────────────── */

static void add_explosion_cell(GameState *s, uint8_t x, uint8_t y, uint8_t owner)
{
    if (s->explosion_count >= MAX_EXPLOSIONS) return; /* defensive cap */
    ExplosionCell *new_cell = &s->explosions[s->explosion_count++];
    new_cell->x = x;
    new_cell->y = y;
    new_cell->owner_id = owner;
    new_cell->ticks_remaining = s->map.explosion_duration;
}

/* ── Bonus application ──────────────────────────────────────────────────── */

static void apply_bonus(Player *player, BonusType bonus_type)
{
    switch (bonus_type) {
    case BONUS_SHIELD: player->shielded   = true; break;
    case BONUS_KICK:   player->can_kick   = true; break;
    case BONUS_MEGA:   player->mega_next = true; break;
    }
}

/* ── Explosion trigger (recursive for chain detonation) ────────────────── */

static void trigger_explosion(GameState *s, int bomb_idx)
{
    if (bomb_idx < 0 || bomb_idx >= s->bomb_count) return;
    if (!s->bombs[bomb_idx].active) return;

    /* Capture a copy before we mark it inactive */
    Bomb bomb = s->bombs[bomb_idx];
    s->bombs[bomb_idx].active = false;

    /* Refund owner's bomb slot */
    s->players[bomb.owner_id].bombs_available++;

    /* Explosion at the bomb's own cell */
    add_explosion_cell(s, bomb.x, bomb.y, bomb.owner_id);

    /* Spread in 4 directions */
    const int dx[4] = { 0,  0, -1,  1};
    const int dy[4] = {-1,  1,  0,  0};

    for (int dir = 0; dir < 4; dir++) {
        for (uint8_t step = 1; step <= bomb.radius; step++) {
            int nx = (int)bomb.x + dx[dir] * step;
            int ny = (int)bomb.y + dy[dir] * step;

            /* Out of bounds — stop this ray */
            if (nx < 0 || nx >= s->map.width || ny < 0 || ny >= s->map.height)
                break;

            CellType ct = cell_at(s, (uint8_t)nx, (uint8_t)ny);

            if (ct == CELL_HARD_WALL) {
                break; /* blocked, no explosion here */
            }

            if (ct == CELL_SOFT_BLOCK) {
                set_cell(s, (uint8_t)nx, (uint8_t)ny, CELL_EMPTY);
                add_explosion_cell(s, (uint8_t)nx, (uint8_t)ny, bomb.owner_id);
                s->players[bomb.owner_id].blocks_destroyed++;
                if (!bomb.full_range) break; /* normal bomb stops here */
                /* full_range bomb burns through — ray continues */
            }

            if (ct == CELL_BONUS) {
                set_cell(s, (uint8_t)nx, (uint8_t)ny, CELL_EMPTY);
                /* bonus_types entry is stale but never read for CELL_EMPTY */
            }

            /* CELL_EMPTY or CELL_BONUS — add explosion, continue ray */
            add_explosion_cell(s, (uint8_t)nx, (uint8_t)ny, bomb.owner_id);

            /* Chain-detonate any bomb sitting here */
            int chain = find_bomb_at(s, (uint8_t)nx, (uint8_t)ny);
            if (chain != -1)
                trigger_explosion(s, chain);
        }
    }
}

/* ── Win/draw check ─────────────────────────────────────────────────────── */

static void check_win(GameState *s)
{
    if (s->game_over) return;

    int alive_count = 0;
    int last_alive  = NO_WINNER;
    for (int i = 0; i < s->player_count; i++) {
        if (s->players[i].alive) {
            alive_count++;
            last_alive = i;
        }
    }

    if (alive_count == 0) {
        s->game_over = true;
        s->winner    = (int8_t)NO_WINNER;
    } else if (alive_count == 1) {
        s->game_over = true;
        s->winner    = (int8_t)last_alive;
    } else if (s->max_ticks > 0 && s->tick_count >= s->max_ticks) {
        s->game_over = true;
        s->winner    = (int8_t)NO_WINNER;
    }
}

/* ── Public API ─────────────────────────────────────────────────────────── */

GameState *game_init(const char *map_file)
{
    GameState *s = calloc(1, sizeof(GameState));
    if (!s) return NULL;

    if (parse_map(map_file, &s->map) != 0) {
        free(s);
        return NULL;
    }

    s->player_count = s->map.player_count;

    for (uint8_t i = 0; i < s->player_count; i++) {
        Player *player        = &s->players[i];
        player->x             = s->map.spawn_x[i];
        player->y             = s->map.spawn_y[i];
        player->speed         = DEFAULT_SPEED;
        player->radius        = DEFAULT_RADIUS;
        player->fuse_time     = DEFAULT_FUSE_TICKS;
        player->bombs_available = DEFAULT_BOMBS;
        player->lives         = DEFAULT_LIVES;
        player->alive         = true;
        player->invincible_ticks = 0;
        /* everything else zero-initialized by calloc */
    }

    s->winner = (int8_t)NO_WINNER;
    return s;
}

void game_tick(GameState *s)
{
    if (!s || s->game_over) return;

    /* ── Step 1: process player actions ─────────────────────────────────── */
    for (int i = 0; i < s->player_count; i++) {
        Player *player = &s->players[i];
        if (!player->alive) continue;

        if (player->move_cooldown > 0)
            player->move_cooldown--;

        if (player->invincible_ticks > 0)
            player->invincible_ticks--;

        if (queue_empty(&player->queue)) continue;

        Action action = queue_dequeue(&player->queue);

        if (action == ACTION_PLACE_BOMB) {
            if (player->bombs_available > 0 &&
                s->bomb_count < MAX_BOMBS &&
                find_bomb_at(s, player->x, player->y) == -1) {
                Bomb *new_bomb          = &s->bombs[s->bomb_count++];
                new_bomb->x             = player->x;
                new_bomb->y             = player->y;
                new_bomb->owner_id      = (uint8_t)i;
                new_bomb->radius        = player->mega_next ? MAX_MAP_WIDTH : player->radius;
                new_bomb->ticks_remaining = player->fuse_time;
                new_bomb->full_range    = player->mega_next;
                new_bomb->active        = true;
                player->mega_next      = false;
                player->bombs_available--;
            }
        } else {
            /* Movement action — only accepted when cooldown is 0 */
            if (player->move_cooldown > 0) continue;

            int dx = 0, dy = 0;
            switch (action) {
            case ACTION_MOVE_UP:    dy = -1; break;
            case ACTION_MOVE_DOWN:  dy =  1; break;
            case ACTION_MOVE_LEFT:  dx = -1; break;
            case ACTION_MOVE_RIGHT: dx =  1; break;
            default: break;
            }
            int next_x = (int)player->x + dx;
            int next_y = (int)player->y + dy;

            /* Bounds check */
            if (next_x < 0 || next_x >= s->map.width ||
                next_y < 0 || next_y >= s->map.height)
                continue;

            CellType ct = cell_at(s, (uint8_t)next_x, (uint8_t)next_y);
            if (ct == CELL_HARD_WALL || ct == CELL_SOFT_BLOCK) continue;

            /* Bomb in target cell — kick if able, else blocked */
            int bomb_there = find_bomb_at(s, (uint8_t)next_x, (uint8_t)next_y);
            if (bomb_there != -1) {
                if (!player->can_kick) continue;

                /* Slide the bomb as far as it can go in the same direction */
                int slide_x = next_x, slide_y = next_y;
                while (true) {
                    int test_x = slide_x + dx, test_y = slide_y + dy;
                    if (test_x < 0 || test_x >= s->map.width ||
                        test_y < 0 || test_y >= s->map.height) break;

                    CellType tc = cell_at(s, (uint8_t)test_x, (uint8_t)test_y);
                    if (tc != CELL_EMPTY && tc != CELL_BONUS) break;
                    if (find_bomb_at(s, (uint8_t)test_x, (uint8_t)test_y) != -1) break;

                    /* kicked bomb must stop before another alive player */
                    if (find_alive_player_at(s, (uint8_t)test_x, (uint8_t)test_y, i)) break;

                    slide_x = test_x;
                    slide_y = test_y;
                }

                /* Bomb can't move at all — player is blocked too */
                if (slide_x == next_x && slide_y == next_y) continue;

                s->bombs[bomb_there].x = (uint8_t)slide_x;
                s->bombs[bomb_there].y = (uint8_t)slide_y;
                /* fall through: player moves into the now-vacated cell */
            }

            player->x = (uint8_t)next_x;
            player->y = (uint8_t)next_y;
            player->move_cooldown = (uint16_t)(TICKS_PER_SECOND / player->speed);

            if (ct == CELL_BONUS) {
                BonusType bonus_type = bonus_at(s, (uint8_t)next_x, (uint8_t)next_y);
                apply_bonus(player, bonus_type);
                player->bonuses_collected++;
                set_cell(s, (uint8_t)next_x, (uint8_t)next_y, CELL_EMPTY);
            }
        }
    }

    /* ── Step 2: tick bombs ──────────────────────────────────────────────── */
    int snap_count = s->bomb_count; /* don't process bombs added this tick */
    for (int i = 0; i < snap_count; i++) {
        if (!s->bombs[i].active) continue;
        if (s->bombs[i].ticks_remaining == 0) continue; /* already triggered */
        s->bombs[i].ticks_remaining--;
        if (s->bombs[i].ticks_remaining == 0)
            trigger_explosion(s, i);
    }

    /* Compact inactive bombs (swap-with-last) */
    int i = 0;
    while (i < s->bomb_count) {
        if (!s->bombs[i].active) {
            s->bombs[i] = s->bombs[s->bomb_count - 1];
            s->bomb_count--;
        } else {
            i++;
        }
    }

    /* ── Step 3: damage players caught in explosions ─────────────────────── */
    for (int player_idx = 0; player_idx < s->player_count; player_idx++) {
        Player *player = &s->players[player_idx];
        if (!player->alive) continue;

        for (int exp_idx = 0; exp_idx < s->explosion_count; exp_idx++) {
            if (s->explosions[exp_idx].x == player->x &&
                s->explosions[exp_idx].y == player->y) {
                if (player->invincible_ticks > 0) {
                    break; /* already protected from this lingering blast */
                }

                if (player->shielded) {
                    player->shielded = false; /* shield absorbs the hit */
                    player->invincible_ticks = s->map.explosion_duration;
                } else {
                    uint8_t killer_id = s->explosions[exp_idx].owner_id;
                    player->lives--;

                    if (player->lives == 0) {
                        /* Transfer the dead player's active bonuses to their killer */
                        if (killer_id != (uint8_t)player_idx) {
                            Player *killer_player = &s->players[killer_id];
                            if (player->can_kick)   killer_player->can_kick   = true;
                            if (player->shielded)   killer_player->shielded   = true;
                            if (player->mega_next) killer_player->mega_next = true;
                            s->players[killer_id].kills++;
                        }
                        player->alive = false;

                    } else {
                        /* Lost a life — respawn at starting position and reset all bonuses */
                        player->x              = s->map.spawn_x[player_idx];
                        player->y              = s->map.spawn_y[player_idx];
                        player->invincible_ticks = RESPAWN_INVINCIBLE_TICKS;
                        player->can_kick       = false;
                        player->shielded       = false;
                        player->mega_next     = false;
                        player->speed          = DEFAULT_SPEED;
                        player->radius         = DEFAULT_RADIUS;
                        player->fuse_time      = DEFAULT_FUSE_TICKS;
                    }
                }
                break; /* one hit per tick per player */
            }
        }
    }

    /* ── Step 4: tick explosions ─────────────────────────────────────────── */
    i = 0;
    while (i < s->explosion_count) {
        s->explosions[i].ticks_remaining--;
        if (s->explosions[i].ticks_remaining == 0) {
            s->explosions[i] = s->explosions[s->explosion_count - 1];
            s->explosion_count--;
        } else {
            i++;
        }
    }

    /* ── Step 5: check win/draw ──────────────────────────────────────────── */
    check_win(s);

    /* ── Step 6: advance clock ───────────────────────────────────────────── */
    s->tick_count++;
}

void game_queue_action(GameState *s, uint8_t player_id, Action action)
{
    if (!s) return;
    if (player_id >= s->player_count) return;
    if (!s->players[player_id].alive) return;
    queue_enqueue(&s->players[player_id].queue, action);
}

bool game_is_over(GameState *s)
{
    return s ? s->game_over : true;
}

int game_get_winner(GameState *s)
{
    return s ? (int)s->winner : NO_WINNER;
}

void game_free(GameState *s)
{
    if (!s) return;
    map_free(&s->map);
    free(s);
}

GameState *game_init_random(uint8_t width, uint8_t height,
                             uint8_t player_count, unsigned int seed,
                             GameMode mode)
{
    GameState *s = calloc(1, sizeof(GameState));
    if (!s) return NULL;

    if (map_generate(&s->map, width, height, player_count, seed, mode) != 0) {
        free(s);
        return NULL;
    }

    s->player_count = player_count;

    for (uint8_t i = 0; i < s->player_count; i++) {
        Player *player          = &s->players[i];
        player->x               = s->map.spawn_x[i];
        player->y               = s->map.spawn_y[i];
        player->speed           = DEFAULT_SPEED;
        player->radius          = DEFAULT_RADIUS;
        player->fuse_time       = DEFAULT_FUSE_TICKS;
        player->bombs_available = DEFAULT_BOMBS;
        player->lives           = DEFAULT_LIVES;
        player->alive           = true;
    }

    s->winner = (int8_t)NO_WINNER;
    return s;
}

#include "game.h"
#include "map.h"
#include <stdlib.h>
#include <string.h>

/* ── Action queue helpers ───────────────────────────────────────────────── */

static bool queue_empty(const ActionQueue *q)
{
    return q->count == 0;
}

static void queue_enqueue(ActionQueue *q, Action a)
{
    if (q->count == ACTION_QUEUE_SIZE) return; /* full — drop */
    q->buf[q->tail & (ACTION_QUEUE_SIZE - 1)] = a;
    q->tail++;
    q->count++;
}

static Action queue_dequeue(ActionQueue *q)
{
    Action a = q->buf[q->head & (ACTION_QUEUE_SIZE - 1)];
    q->head++;
    q->count--;
    return a;
}

/* ── Map/bomb lookup helpers ────────────────────────────────────────────── */

static inline CellType cell_at(const GameState *s, uint8_t x, uint8_t y)
{
    return s->map.cells[(size_t)y * s->map.width + x];
}

static inline void set_cell(GameState *s, uint8_t x, uint8_t y, CellType t)
{
    s->map.cells[(size_t)y * s->map.width + x] = t;
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
    ExplosionCell *e = &s->explosions[s->explosion_count++];
    e->x = x;
    e->y = y;
    e->owner_id = owner;
    e->ticks_remaining = s->map.explosion_duration;
}

/* ── Bonus application ──────────────────────────────────────────────────── */

static void apply_bonus(Player *p, BonusType bt)
{
    switch (bt) {
    case BONUS_SPEED:
        p->speed = (uint8_t)(p->speed + SPEED_BONUS_DELTA);
        break;
    case BONUS_RADIUS:
        p->radius = (uint8_t)(p->radius + RADIUS_BONUS_DELTA);
        break;
    case BONUS_FUSE:
        p->fuse_time = (uint16_t)(p->fuse_time + FUSE_BONUS_DELTA);
        break;
    case BONUS_SHIELD:
        p->shielded = true;
        break;
    case BONUS_KICK:
        p->can_kick = true;
        break;
    case BONUS_RAPID:
        p->rapid_next = true;
        break;
    }
}

/* ── Explosion trigger (recursive for chain detonation) ────────────────── */

static void trigger_explosion(GameState *s, int bomb_idx)
{
    if (bomb_idx < 0 || bomb_idx >= s->bomb_count) return;
    if (!s->bombs[bomb_idx].active) return;

    /* Capture a copy before we mark it inactive */
    Bomb b = s->bombs[bomb_idx];
    s->bombs[bomb_idx].active = false;

    /* Refund owner's bomb slot */
    s->players[b.owner_id].bombs_available++;

    /* Explosion at the bomb's own cell */
    add_explosion_cell(s, b.x, b.y, b.owner_id);

    /* Spread in 4 directions */
    const int dx[4] = { 0,  0, -1,  1};
    const int dy[4] = {-1,  1,  0,  0};

    for (int dir = 0; dir < 4; dir++) {
        for (uint8_t step = 1; step <= b.radius; step++) {
            int nx = (int)b.x + dx[dir] * step;
            int ny = (int)b.y + dy[dir] * step;

            /* Out of bounds — stop this ray */
            if (nx < 0 || nx >= s->map.width || ny < 0 || ny >= s->map.height)
                break;

            CellType ct = cell_at(s, (uint8_t)nx, (uint8_t)ny);

            if (ct == CELL_HARD_WALL) {
                break; /* blocked, no explosion here */
            }

            if (ct == CELL_SOFT_BLOCK) {
                set_cell(s, (uint8_t)nx, (uint8_t)ny, CELL_EMPTY);
                add_explosion_cell(s, (uint8_t)nx, (uint8_t)ny, b.owner_id);
                s->players[b.owner_id].blocks_destroyed++;
                if (!b.full_range) break; /* normal bomb stops here */
                /* full_range bomb burns through — ray continues */
            }

            if (ct == CELL_BONUS) {
                set_cell(s, (uint8_t)nx, (uint8_t)ny, CELL_EMPTY);
                /* bonus_types entry is stale but never read for CELL_EMPTY */
            }

            /* CELL_EMPTY or CELL_BONUS — add explosion, continue ray */
            add_explosion_cell(s, (uint8_t)nx, (uint8_t)ny, b.owner_id);

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
        Player *p        = &s->players[i];
        p->x             = s->map.spawn_x[i];
        p->y             = s->map.spawn_y[i];
        p->speed         = DEFAULT_SPEED;
        p->radius        = DEFAULT_RADIUS;
        p->fuse_time     = DEFAULT_FUSE_TICKS;
        p->bombs_available = DEFAULT_BOMBS;
        p->lives         = DEFAULT_LIVES;
        p->alive         = true;
        p->invincible_ticks = 0;
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
        Player *p = &s->players[i];
        if (!p->alive) continue;

        if (p->move_cooldown > 0)
            p->move_cooldown--;
        

        if (p->invincible_ticks > 0)
            p->invincible_ticks--;
        

        if (queue_empty(&p->queue)) continue;

        Action a = queue_dequeue(&p->queue);

        if (a == ACTION_PLACE_BOMB) {
            if (p->bombs_available > 0 &&
                s->bomb_count < MAX_BOMBS &&
                find_bomb_at(s, p->x, p->y) == -1) {
                Bomb *bm          = &s->bombs[s->bomb_count++];
                bm->x             = p->x;
                bm->y             = p->y;
                bm->owner_id      = (uint8_t)i;
                bm->radius        = p->rapid_next ? MAX_MAP_WIDTH : p->radius;
                bm->ticks_remaining = p->fuse_time;
                bm->full_range    = p->rapid_next;
                bm->active        = true;
                p->rapid_next     = false;
                p->bombs_available--;
            }
        } else {
            /* Movement action — only accepted when cooldown is 0 */
            if (p->move_cooldown > 0) continue;

            int dx = 0, dy = 0;
            switch (a) {
            case ACTION_MOVE_UP:    dy = -1; break;
            case ACTION_MOVE_DOWN:  dy =  1; break;
            case ACTION_MOVE_LEFT:  dx = -1; break;
            case ACTION_MOVE_RIGHT: dx =  1; break;
            default: break;
            }
            int nx = (int)p->x + dx;
            int ny = (int)p->y + dy;

            /* Bounds check */
            if (nx < 0 || nx >= s->map.width || ny < 0 || ny >= s->map.height)
                continue;

            CellType ct = cell_at(s, (uint8_t)nx, (uint8_t)ny);
            if (ct == CELL_HARD_WALL || ct == CELL_SOFT_BLOCK) continue;

            /* Bomb in target cell — kick if able, else blocked */
            int bomb_there = find_bomb_at(s, (uint8_t)nx, (uint8_t)ny);
            if (bomb_there != -1) {
                if (!p->can_kick) continue;

                /* Slide the bomb as far as it can go in the same direction */
                int sx = nx, sy = ny;
                while (true) {
                    int tx = sx + dx, ty = sy + dy;
                    if (tx < 0 || tx >= s->map.width ||
                        ty < 0 || ty >= s->map.height) break;

                    CellType tc = cell_at(s, (uint8_t)tx, (uint8_t)ty);
                    if (tc != CELL_EMPTY && tc != CELL_BONUS) break;
                    if (find_bomb_at(s, (uint8_t)tx, (uint8_t)ty) != -1) break;

                    /* kicked bomb must stop before another alive player */
                    if (find_alive_player_at(s, (uint8_t)tx, (uint8_t)ty, i)) break;

                    sx = tx;
                    sy = ty;
                }            

                /* Bomb can't move at all — player is blocked too */
                if (sx == nx && sy == ny) continue;

                s->bombs[bomb_there].x = (uint8_t)sx;
                s->bombs[bomb_there].y = (uint8_t)sy;
                /* fall through: player moves into the now-vacated cell */
            }

            p->x = (uint8_t)nx;
            p->y = (uint8_t)ny;
            p->move_cooldown = (uint16_t)(TICKS_PER_SECOND / p->speed);

            if (ct == CELL_BONUS) {
                BonusType bt = bonus_at(s, (uint8_t)nx, (uint8_t)ny);
                apply_bonus(p, bt);
                p->bonuses_collected++;
                set_cell(s, (uint8_t)nx, (uint8_t)ny, CELL_EMPTY);
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
    for (int p = 0; p < s->player_count; p++) {
        Player *pl = &s->players[p];
        if (!pl->alive) continue;

        for (int e = 0; e < s->explosion_count; e++) {
            if (s->explosions[e].x == pl->x && s->explosions[e].y == pl->y) {
                if (pl->invincible_ticks > 0) {
                    break; /* already protected from this lingering blast */
                }

                if (pl->shielded) {
                    pl->shielded = false; /* shield absorbs the hit */
                    pl->invincible_ticks = s->map.explosion_duration;
                } else {
                    uint8_t killer = s->explosions[e].owner_id;
                    pl->lives--;

                    if (pl->lives == 0) {
                        /* Transfer the dead player's active bonuses to their killer */
                        if (killer != (uint8_t)p) {
                            Player *k = &s->players[killer];
                            if (pl->can_kick)                        k->can_kick    = true;
                            if (pl->shielded)                        k->shielded    = true;
                            if (pl->rapid_next)                      k->rapid_next  = true;
                            if (pl->speed    > DEFAULT_SPEED)        k->speed      += pl->speed    - DEFAULT_SPEED;
                            if (pl->radius   > DEFAULT_RADIUS)       k->radius     += pl->radius   - DEFAULT_RADIUS;
                            if (pl->fuse_time > DEFAULT_FUSE_TICKS)  k->fuse_time  += pl->fuse_time - DEFAULT_FUSE_TICKS;
                            s->players[killer].kills++;
                        }
                        pl->alive = false;

                    } else {
                        /* Lost a life — respawn at starting position and reset all bonuses */
                        pl->x              = s->map.spawn_x[p];
                        pl->y              = s->map.spawn_y[p];
                        pl->invincible_ticks = RESPAWN_INVINCIBLE_TICKS;
                        pl->can_kick       = false;
                        pl->shielded       = false;
                        pl->rapid_next     = false;
                        pl->speed          = DEFAULT_SPEED;
                        pl->radius         = DEFAULT_RADIUS;
                        pl->fuse_time      = DEFAULT_FUSE_TICKS;
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
        Player *p          = &s->players[i];
        p->x               = s->map.spawn_x[i];
        p->y               = s->map.spawn_y[i];
        p->speed           = DEFAULT_SPEED;
        p->radius          = DEFAULT_RADIUS;
        p->fuse_time       = DEFAULT_FUSE_TICKS;
        p->bombs_available = DEFAULT_BOMBS;
        p->lives           = DEFAULT_LIVES;
        p->alive           = true;
    }

    s->winner = (int8_t)NO_WINNER;
    return s;
}

#ifndef NET_H
#define NET_H

/*
 * net.h — shared network protocol for Bomberman online PvP.
 *
 * Architecture
 * ────────────
 *   bomberman_server  runs game logic at 20 ticks/s, sends state to clients
 *   bomberman_client  sends keyboard input, renders the received state
 *
 * Transport: TCP (one persistent connection per player)
 * All multi-byte integers travel in network byte order — use htons/htonl on
 * the sender and ntohs/ntohl on the receiver.
 *
 * Session flow
 * ────────────
 *   1. Client connects, sends PKT_JOIN  (name)
 *   2. Server assigns a player_id, replies with PKT_WELCOME  (id + map data)
 *   3. Server waits until all expected players have joined
 *   4. Server broadcasts PKT_START  (no payload)
 *   5. Every tick: server broadcasts PKT_STATE  (full snapshot)
 *   6. At any time: client sends PKT_ACTION  (one key press)
 *   7. Game ends: server broadcasts PKT_GAMEOVER, both sides close
 *
 * Build-time overrides (set via Makefile or command line):
 *   make PORT=9999 SERVER_IP=10.0.0.5 client
 */

#include "game.h"
#include <stdint.h>
#include <stdbool.h>

/* ── Port ───────────────────────────────────────────────────────────────── */

#ifndef NET_PORT
#define NET_PORT 54321
#endif

#ifndef NET_DEFAULT_HOST
#define NET_DEFAULT_HOST "127.0.0.1"
#endif

/* ── Packet type tag (first byte of every message) ─────────────────────── */

typedef enum {
    PKT_JOIN     = 0x01,  /* client → server */
    PKT_WELCOME  = 0x02,  /* server → client */
    PKT_START    = 0x03,  /* server → client, no payload */
    PKT_ACTION   = 0x04,  /* client → server */
    PKT_STATE    = 0x05,  /* server → client */
    PKT_GAMEOVER = 0x06,  /* server → client */
} PacketType;

/* ── Sub-structs used inside PktState ───────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint8_t  x, y;
    uint8_t  lives;
    uint16_t invincible_ticks;
    uint8_t  alive;      /* bool packed as byte */
    uint8_t  shielded;
    uint8_t  can_kick;
    uint8_t  mega_next;
} PlayerSnap;

typedef struct __attribute__((packed)) {
    uint8_t  x, y, owner_id;
    uint16_t ticks_remaining;
    uint8_t  full_range;
} BombSnap;

typedef struct __attribute__((packed)) {
    uint8_t x, y;
} ExplosionSnap;

typedef struct __attribute__((packed)) {
    uint8_t x, y;
    uint8_t cell_type;   /* CellType value after the change */
    uint8_t bonus_type;  /* BonusType — only valid when cell_type == CELL_BONUS */
} CellChange;

/* ── Packet payloads ────────────────────────────────────────────────────── */

/* PKT_JOIN: sent once when a client connects */
typedef struct __attribute__((packed)) {
    char name[16];   /* player display name, null-terminated */
} PktJoin;

/*
 * PKT_WELCOME: server response to PKT_JOIN.
 * The fixed header is followed immediately by two flat byte arrays:
 *   map_width * map_height  bytes  →  CellType  cells[]
 *   map_width * map_height  bytes  →  BonusType bonus_types[]
 * Read those after receiving sizeof(PktWelcome) bytes.
 */
typedef struct __attribute__((packed)) {
    uint8_t  player_id;
    uint8_t  player_count;
    uint8_t  map_width;
    uint8_t  map_height;
    uint16_t explosion_duration;
    uint8_t  spawn_x[MAX_PLAYERS];
    uint8_t  spawn_y[MAX_PLAYERS];
} PktWelcome;

/* PKT_START: no payload — the type byte alone signals "game begins" */

/* PKT_ACTION: one key press from a client */
typedef struct __attribute__((packed)) {
    uint8_t player_id;
    uint8_t action;   /* Action enum value */
} PktAction;

/* PKT_STATE: full per-tick snapshot broadcast by the server */
typedef struct __attribute__((packed)) {
    uint32_t     tick_count;
    uint8_t      game_over;
    int8_t       winner;

    uint8_t      player_count;
    PlayerSnap   players[MAX_PLAYERS];

    uint8_t      bomb_count;
    BombSnap     bombs[MAX_BOMBS];

    uint16_t     explosion_count;
    ExplosionSnap explosions[MAX_EXPLOSIONS];

    /* Cells that changed this tick (blocks destroyed, bonuses picked up) */
    uint8_t     changed_cell_count;
    CellChange  changed_cells[64];
} PktState;

/* PKT_GAMEOVER: final result */
typedef struct __attribute__((packed)) {
    int8_t winner;     /* NO_WINNER or winning player_id */
    char   name[16];   /* winner name, or "DRAW" */
} PktGameOver;

/* ── Utility: apply a received PktState snapshot to a local GameState ───── */
/*
 * Call on the client after receiving PKT_STATE.
 * The map inside dst must already be populated from PKT_WELCOME.
 * This function updates players, bombs, explosions, and changed cells.
 */
void net_apply_state(GameState *dst, const PktState *snap);

#endif /* NET_H */
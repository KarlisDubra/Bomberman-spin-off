#include "net.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>

/* ── Connect to the server ──────────────────────────────────────────────── */

/*
 * Open a TCP connection to 'host':'port'.
 * Returns the socket fd, or -1 on failure.
 */
int net_client_connect(const char *host, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons((uint16_t)port),
    };
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", host);
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }
    printf("Connected to %s:%d\n", host, port);
    return fd;
}

/* ── Low-level helpers (mirrored from net_server.c) ─────────────────────── */

static int send_all(int fd, const void *buf, size_t len)
{
    const char *ptr = buf;
    while (len > 0) {
        ssize_t n = send(fd, ptr, len, 0);
        if (n <= 0) return -1;
        ptr += n; len -= (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
    char *ptr = buf;
    while (len > 0) {
        ssize_t n = recv(fd, ptr, len, 0);
        if (n <= 0) return -1;
        ptr += n; len -= (size_t)n;
    }
    return 0;
}

/* ── Handshake ───────────────────────────────────────────────────────────── */

/*
 * Send PKT_JOIN, wait for PKT_WELCOME, populate the map inside 'state'.
 * Returns the assigned player_id, or -1 on failure.
 *
 * 'fd'    — socket returned by net_client_connect()
 * 'name'  — display name for this player (max 15 chars)
 * 'state' — caller provides an empty GameState; this function fills its map
 *           and player_count.  Caller must later call game_init_players().
 */
int net_client_join(int fd, const char *name, GameState *state)
{
    /* Send PKT_JOIN */
    uint8_t tag = PKT_JOIN;
    PktJoin join = {0};
    strncpy(join.name, name, sizeof(join.name) - 1);
    if (send_all(fd, &tag, 1) < 0) return -1;
    if (send_all(fd, &join, sizeof(join)) < 0) return -1;

    /* Receive PKT_WELCOME */
    if (recv_all(fd, &tag, 1) < 0 || tag != PKT_WELCOME) return -1;
    PktWelcome w;
    if (recv_all(fd, &w, sizeof(w)) < 0) return -1;

    /* Build the map */
    state->map.width              = w.map_width;
    state->map.height             = w.map_height;
    state->map.explosion_duration = ntohs(w.explosion_duration);
    state->map.player_count       = w.player_count;
    state->player_count           = w.player_count;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        state->map.spawn_x[i] = w.spawn_x[i];
        state->map.spawn_y[i] = w.spawn_y[i];
    }

    size_t ncells = (size_t)w.map_width * w.map_height;
    state->map.cells       = calloc(ncells, sizeof(CellType));
    state->map.bonus_types = calloc(ncells, sizeof(BonusType));
    if (!state->map.cells || !state->map.bonus_types) return -1;

    if (recv_all(fd, state->map.cells,       ncells * sizeof(CellType))  < 0) return -1;
    if (recv_all(fd, state->map.bonus_types, ncells * sizeof(BonusType)) < 0) return -1;

    printf("Joined as player %d  (map %dx%d)\n",
           w.player_id, w.map_width, w.map_height);
    return (int)w.player_id;
}

/*
 * Block until the server sends PKT_START.
 * Returns 0 when the game begins, -1 on error.
 */
int net_client_wait_start(int fd)
{
    uint8_t tag;
    if (recv_all(fd, &tag, 1) < 0 || tag != PKT_START) return -1;
    printf("Game started!\n");
    return 0;
}

/* ── Per-frame send ──────────────────────────────────────────────────────── */

/*
 * Send a single action to the server.
 * Call this immediately when the local player presses a key.
 */
int net_client_send_action(int fd, uint8_t player_id, Action action)
{
    uint8_t tag = PKT_ACTION;
    PktAction act = { .player_id = player_id, .action = (uint8_t)action };
    if (send_all(fd, &tag, 1) < 0) return -1;
    if (send_all(fd, &act, sizeof(act)) < 0) return -1;
    return 0;
}

/* ── Per-frame receive ───────────────────────────────────────────────────── */

/*
 * Non-blocking check for a PKT_STATE or PKT_GAMEOVER packet.
 * Returns:
 *    1  — PKT_STATE received; 'state' has been updated, call render_frame()
 *    0  — nothing available yet (normal during a frame)
 *   -1  — connection error
 *   -2  — PKT_GAMEOVER received; 'go_out' is populated, show end screen
 */
int net_client_recv(int fd, GameState *state, PktGameOver *go_out)
{
    /* Non-blocking peek for the type tag */
    int flags_orig = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags_orig | O_NONBLOCK);
    uint8_t tag;
    ssize_t n = recv(fd, &tag, 1, 0);
    fcntl(fd, F_SETFL, flags_orig);

    // if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) return -1;
    if (n < 0) return 0; /* EAGAIN — nothing ready */

    if (tag == PKT_STATE) {
        PktState snap;
        if (recv_all(fd, &snap, sizeof(snap)) < 0) return -1;
        net_apply_state(state, &snap);
        return 1;
    }
    if (tag == PKT_GAMEOVER) {
        if (recv_all(fd, go_out, sizeof(*go_out)) < 0) return -1;
        return -2;
    }
    return 0; /* unknown tag — ignore */
}

/* ── Apply snapshot to local GameState ──────────────────────────────────── */

/*
 * Translate a PktState (network byte order) into the local GameState
 * that is passed to render_frame() each frame.
 * The map pointer inside 'dst' must remain valid (allocated during join).
 */
void net_apply_state(GameState *dst, const PktState *snap)
{
    dst->tick_count      = ntohl(snap->tick_count);
    dst->game_over       = snap->game_over;
    dst->winner          = snap->winner;
    dst->player_count    = snap->player_count;

    for (int i = 0; i < snap->player_count; i++) {
        dst->players[i].x                = snap->players[i].x;
        dst->players[i].y                = snap->players[i].y;
        dst->players[i].lives            = snap->players[i].lives;
        dst->players[i].invincible_ticks = ntohs(snap->players[i].invincible_ticks);
        dst->players[i].alive            = snap->players[i].alive;
        dst->players[i].shielded         = snap->players[i].shielded;
        dst->players[i].can_kick         = snap->players[i].can_kick;
        dst->players[i].mega_next        = snap->players[i].mega_next;
    }

    dst->bomb_count = snap->bomb_count;
    for (int i = 0; i < snap->bomb_count; i++) {
        dst->bombs[i].x               = snap->bombs[i].x;
        dst->bombs[i].y               = snap->bombs[i].y;
        dst->bombs[i].owner_id        = snap->bombs[i].owner_id;
        dst->bombs[i].ticks_remaining = ntohs(snap->bombs[i].ticks_remaining);
        dst->bombs[i].full_range      = snap->bombs[i].full_range;
        dst->bombs[i].active          = true;
    }

    dst->explosion_count = ntohs(snap->explosion_count);
    for (int i = 0; i < dst->explosion_count; i++) {
        dst->explosions[i].x = snap->explosions[i].x;
        dst->explosions[i].y = snap->explosions[i].y;
    }

    /* Apply map cell changes */
    for (int i = 0; i < snap->changed_cell_count; i++) {
        size_t idx = (size_t)snap->changed_cells[i].y * dst->map.width
                   + snap->changed_cells[i].x;
        dst->map.cells[idx]       = (CellType) snap->changed_cells[i].cell_type;
        dst->map.bonus_types[idx] = (BonusType)snap->changed_cells[i].bonus_type;
    }
}
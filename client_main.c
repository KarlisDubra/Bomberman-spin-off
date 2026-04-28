#define _POSIX_C_SOURCE 199309L
#include "game.h"
#include "map.h"
#include "render.h"
#include "net.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── Session globals ─────────────────────────────────────────────────────── */

static GameState  g_state;           /* local mirror updated each tick */
static int        g_server_fd  = -1; /* TCP socket to the server */
static uint8_t    g_player_id  = 0;  /* assigned by server during handshake */
static char       g_name[16]   = "PLAYER";

/* ── GLFW key callback ───────────────────────────────────────────────────── */

/*
 * On each key press the client immediately sends a PKT_ACTION to the server.
 * Each physical machine controls exactly one player (g_player_id).
 * Change the key bindings here if needed.
 */
static void key_callback(GLFWwindow *win, int key, int scancode,
                         int action, int mods)
{
    (void)scancode; (void)mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    Action act;
    switch (key) {
    case GLFW_KEY_UP:    act = ACTION_MOVE_UP;    break;
    case GLFW_KEY_DOWN:  act = ACTION_MOVE_DOWN;  break;
    case GLFW_KEY_LEFT:  act = ACTION_MOVE_LEFT;  break;
    case GLFW_KEY_RIGHT: act = ACTION_MOVE_RIGHT; break;
    case GLFW_KEY_SPACE:
    case GLFW_KEY_ENTER: act = ACTION_PLACE_BOMB; break;
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(win, GLFW_TRUE);
        return;
    default: return;
    }

    net_client_send_action(g_server_fd, g_player_id, act);
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

/*
 * Usage:
 *   ./bomberman_client [server_ip] [player_name]
 *
 * Examples:
 *   ./bomberman_client 192.168.1.5 Alice
 *   ./bomberman_client                         ← connects to 127.0.0.1 as "PLAYER"
 *
 * Override port at build time:
 *   make PORT=9999 SERVER_IP=10.0.0.2 client
 */
int main(int argc, char *argv[])
{
    const char *host = NET_DEFAULT_HOST;
    if (argc > 1) host = argv[1];
    if (argc > 2) strncpy(g_name, argv[2], sizeof(g_name) - 1);

    /* Connect to server */
    g_server_fd = net_client_connect(host, NET_PORT);
    if (g_server_fd < 0) return 1;

    /* Handshake: send name, receive player_id + map */
    memset(&g_state, 0, sizeof(g_state));
    g_state.winner = (int8_t)NO_WINNER;

    int pid = net_client_join(g_server_fd, g_name, &g_state);
    if (pid < 0) { fprintf(stderr, "Join failed\n"); close(g_server_fd); return 1; }
    g_player_id = (uint8_t)pid;

    /* Initialise player structs (positions from map spawns, default stats) */
    for (uint8_t i = 0; i < g_state.player_count; i++) {
        g_state.players[i].x               = g_state.map.spawn_x[i];
        g_state.players[i].y               = g_state.map.spawn_y[i];
        g_state.players[i].speed           = DEFAULT_SPEED;
        g_state.players[i].radius          = DEFAULT_RADIUS;
        g_state.players[i].fuse_time       = DEFAULT_FUSE_TICKS;
        g_state.players[i].bombs_available = DEFAULT_BOMBS;
        g_state.players[i].lives           = DEFAULT_LIVES;
        g_state.players[i].alive           = true;
    }

    /* Wait for all players to be ready */
    if (net_client_wait_start(g_server_fd) < 0) {
        fprintf(stderr, "Did not receive START\n");
        close(g_server_fd);
        return 1;
    }

    /* ── Window + renderer ─────────────────────────────────────────────────── */
    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    int win_w = g_state.map.width  * CELL_SIZE;
    int win_h = g_state.map.height * CELL_SIZE + HUD_HEIGHT;
    char title[64];
    snprintf(title, sizeof(title), "Bomberman — %s (Player %d)", g_name, g_player_id + 1);

    GLFWwindow *window = glfwCreateWindow(win_w, win_h, title, NULL, NULL);
    if (!window) { fprintf(stderr, "Window creation failed\n"); glfwTerminate(); return 1; }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetKeyCallback(window, key_callback);
    render_init(g_state.map.width, g_state.map.height);

    /* ── Main loop ─────────────────────────────────────────────────────────── */
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        PktGameOver go = {0};
        int status = net_client_recv(g_server_fd, &g_state, &go);

        if (status == -1) {
            /* Server disconnected */
            fprintf(stderr, "Lost connection to server\n");
            break;
        }

        if (status == -2) {
            /* Game over — render final frame then show result */
            render_frame(&g_state);
            char msg[64];
            snprintf(msg, sizeof(msg), "%s", go.name);
            render_game_over(g_state.map.width, g_state.map.height,
                             (int)go.winner, msg);
            snprintf(title, sizeof(title), "Bomberman — %s   [Esc] quit", go.name);
            glfwSetWindowTitle(window, title);
            glfwSwapBuffers(window);

            /* Wait for ESC */
            while (!glfwWindowShouldClose(window)) {
                glfwWaitEvents();
            }
            break;
        }

        /* status == 1: new state received → render; 0: no packet yet, re-render last */
        render_frame(&g_state);

        if (g_state.game_over) {
            char msg[64];
            if (g_state.winner == NO_WINNER)
                snprintf(msg, sizeof(msg), "DRAW");
            else
                snprintf(msg, sizeof(msg), "PLAYER %d WINS", g_state.winner + 1);
            render_game_over(g_state.map.width, g_state.map.height,
                             (int)g_state.winner, msg);
        } else {
            snprintf(title, sizeof(title),
                     "Bomberman — %s (P%d)   Tick %u",
                     g_name, g_player_id + 1, g_state.tick_count);
            glfwSetWindowTitle(window, title);
        }

        glfwSwapBuffers(window);
    }

    render_cleanup();
    close(g_server_fd);
    map_free(&g_state.map);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

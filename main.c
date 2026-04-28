#define _POSIX_C_SOURCE 199309L   /* expose clock_gettime / CLOCK_MONOTONIC */
#include "game.h"
#include "map.h"
#include "render.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Game phase ─────────────────────────────────────────────────────────── */

typedef enum { PHASE_LOBBY, PHASE_PLAYING, PHASE_RESULTS } GamePhase;

/* ── Globals ────────────────────────────────────────────────────────────── */

static GameState  *g_state        = NULL;
static GamePhase   g_phase        = PHASE_LOBBY;
static Map         g_lobby_maps[GAMEMODE_COUNT];
static unsigned    g_lobby_seeds[GAMEMODE_COUNT];
static int         g_selected     = 0;          /* 0 or 1 */
static uint8_t     g_player_count = 4;
static const char *g_map_file     = NULL;        /* NULL = use lobby */

/* Nanosecond-precision seed so rapid restarts always get a different map */
static unsigned int new_seed(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned int)(ts.tv_nsec ^ ((unsigned long)ts.tv_sec << 17));
}

/* Generate (or re-generate) both lobby preview maps */
static void lobby_generate(void)
{
    for (int m = 0; m < GAMEMODE_COUNT; m++) {
        map_free(&g_lobby_maps[m]);
        g_lobby_seeds[m] = new_seed();
        map_generate(&g_lobby_maps[m], 15, 11,
                     g_player_count, g_lobby_seeds[m], (GameMode)m);
    }
}

/* Start a game from the currently selected lobby map */
static void lobby_start(GLFWwindow *win)
{
    game_free(g_state);
    g_state = game_init_random(15, 11, g_player_count,
                               g_lobby_seeds[g_selected],
                               (GameMode)g_selected);
    if (!g_state) {
        fprintf(stderr, "Failed to start game\n");
        glfwSetWindowShouldClose(win, GLFW_TRUE);
        return;
    }
    g_phase = PHASE_PLAYING;
}

/* ── Key callback ───────────────────────────────────────────────────────── */

static void key_callback(GLFWwindow *win, int key, int scancode,
                         int action, int mods)
{
    (void)scancode; (void)mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    /* ── Lobby controls ── */
    if (g_phase == PHASE_LOBBY) {
        switch (key) {
        case GLFW_KEY_LEFT:  case GLFW_KEY_A:
            g_selected = 0; break;
        case GLFW_KEY_RIGHT: case GLFW_KEY_D:
            g_selected = 1; break;
        case GLFW_KEY_1: g_selected = 0; break;
        case GLFW_KEY_2: g_selected = 1; break;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
        case GLFW_KEY_SPACE:
            lobby_start(win); break;
        case GLFW_KEY_R:
            lobby_generate(); break;
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(win, GLFW_TRUE); break;
        default: break;
        }
        return;
    }

    /* ── Results screen: only R and Escape ── */
    if (g_phase == PHASE_RESULTS) {
        if (key == GLFW_KEY_R) {
            game_free(g_state);
            g_state = NULL;
            lobby_generate();
            g_phase = PHASE_LOBBY;
        } else if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(win, GLFW_TRUE);
        }
        return;
    }

    /* ── In-game controls ── */
    if (!g_state) return;

    switch (key) {
    /* Player 0 — Arrow keys + Enter */
    case GLFW_KEY_UP:    game_queue_action(g_state, 0, ACTION_MOVE_UP);    break;
    case GLFW_KEY_DOWN:  game_queue_action(g_state, 0, ACTION_MOVE_DOWN);  break;
    case GLFW_KEY_LEFT:  game_queue_action(g_state, 0, ACTION_MOVE_LEFT);  break;
    case GLFW_KEY_RIGHT: game_queue_action(g_state, 0, ACTION_MOVE_RIGHT); break;
    case GLFW_KEY_ENTER: game_queue_action(g_state, 0, ACTION_PLACE_BOMB); break;

    /* Player 1 — WASD + Q */
    case GLFW_KEY_W:     game_queue_action(g_state, 1, ACTION_MOVE_UP);    break;
    case GLFW_KEY_S:     game_queue_action(g_state, 1, ACTION_MOVE_DOWN);  break;
    case GLFW_KEY_A:     game_queue_action(g_state, 1, ACTION_MOVE_LEFT);  break;
    case GLFW_KEY_D:     game_queue_action(g_state, 1, ACTION_MOVE_RIGHT); break;
    case GLFW_KEY_Q:     game_queue_action(g_state, 1, ACTION_PLACE_BOMB); break;

    /* Player 2 — IJKL + U */
    case GLFW_KEY_I:     game_queue_action(g_state, 2, ACTION_MOVE_UP);    break;
    case GLFW_KEY_K:     game_queue_action(g_state, 2, ACTION_MOVE_DOWN);  break;
    case GLFW_KEY_J:     game_queue_action(g_state, 2, ACTION_MOVE_LEFT);  break;
    case GLFW_KEY_L:     game_queue_action(g_state, 2, ACTION_MOVE_RIGHT); break;
    case GLFW_KEY_U:     game_queue_action(g_state, 2, ACTION_PLACE_BOMB); break;

    /* Player 3 — Numpad 8456 + 0 */
    case GLFW_KEY_KP_8:  game_queue_action(g_state, 3, ACTION_MOVE_UP);    break;
    case GLFW_KEY_KP_5:  game_queue_action(g_state, 3, ACTION_MOVE_DOWN);  break;
    case GLFW_KEY_KP_4:  game_queue_action(g_state, 3, ACTION_MOVE_LEFT);  break;
    case GLFW_KEY_KP_6:  game_queue_action(g_state, 3, ACTION_MOVE_RIGHT); break;
    case GLFW_KEY_KP_0:  game_queue_action(g_state, 3, ACTION_PLACE_BOMB); break;

    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(win, GLFW_TRUE); break;

    default: break;
    }
}

/* ── Entry point ────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    /* Parse args: optional map file, optional player count */
    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        /* Pure number → player count, implies random lobby */
        int n = atoi(a);
        if (n >= 2 && n <= MAX_PLAYERS) {
            g_player_count = (uint8_t)n;
        } else if (strcmp(a, "random") != 0) {
            /* Treat as map file — skip lobby */
            g_map_file = a;
        }
        /* "random" is accepted but we default to lobby anyway */
    }

    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    int win_w, win_h;

    /* If a specific map file was given, skip straight to game */
    if (g_map_file) {
        g_state = game_init(g_map_file);
        if (!g_state) {
            fprintf(stderr, "Failed to load map: %s\n", g_map_file);
            glfwTerminate();
            return 1;
        }
        win_w   = g_state->map.width  * CELL_SIZE;
        win_h   = g_state->map.height * CELL_SIZE + HUD_HEIGHT;
        g_phase = PHASE_PLAYING;
    } else {
        /* Lobby mode — generate preview maps, use fixed window size */
        lobby_generate();
        win_w = LOBBY_WIN_W;
        win_h = LOBBY_WIN_H;
    }

    GLFWwindow *window = glfwCreateWindow(win_w, win_h, "Bomberman", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetKeyCallback(window, key_callback);
    render_init(15, 11);

    printf("Controls:\n");
    printf("  Lobby:  Left/Right (or 1/2) to choose mode\n");
    printf("          Enter/Space to play   R to regenerate\n");
    printf("  P0: Arrows + Enter   P1: WASD + Q\n");
    printf("  P2: IJKL + U         P3: Numpad 8456 + 0\n");
    printf("  R: back to lobby     Esc: quit\n\n");

    double last_tick = glfwGetTime();
    const double TICK_DT = 1.0 / TICKS_PER_SECOND;
    char title[160];

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (g_phase == PHASE_LOBBY) {
            render_lobby(win_w, win_h, g_lobby_maps, g_selected);
            snprintf(title, sizeof(title),
                     "Bomberman — Select mode   Left/Right   Enter to play   R refresh");

        } else if (g_phase == PHASE_PLAYING) {
            double now = glfwGetTime();
            while (!game_is_over(g_state) && now - last_tick >= TICK_DT) {
                game_tick(g_state);
                last_tick += TICK_DT;
            }
            render_frame(g_state);

            if (game_is_over(g_state)) {
                g_phase = PHASE_RESULTS;
            } else {
                snprintf(title, sizeof(title),
                         "Bomberman — Tick %-4u  P0=Arrows  P1=WASD  P2=IJKL  P3=Numpad",
                         g_state->tick_count);
            }

        } else { /* PHASE_RESULTS */
            render_frame(g_state);
            int winner = game_get_winner(g_state);
            char over_msg[32];
            if (winner == NO_WINNER)
                snprintf(over_msg, sizeof(over_msg), "DRAW");
            else
                snprintf(over_msg, sizeof(over_msg), "PLAYER %d WINS", winner + 1);
            render_game_over(g_state->map.width, g_state->map.height, winner, over_msg);
            if (winner == NO_WINNER)
                snprintf(title, sizeof(title),
                         "Bomberman — DRAW   [R] lobby  [Esc] quit");
            else
                snprintf(title, sizeof(title),
                         "Bomberman — Player %d wins!   [R] lobby  [Esc] quit", winner);
        }

        glfwSetWindowTitle(window, title);
        glfwSwapBuffers(window);
    }

    render_cleanup();
    game_free(g_state);
    for (int m = 0; m < GAMEMODE_COUNT; m++)
        map_free(&g_lobby_maps[m]);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

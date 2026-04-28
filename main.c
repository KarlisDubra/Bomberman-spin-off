#include "game.h"
#include "net.h"
#include "render.h"

#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { PHASE_LOBBY, PHASE_PLAYING, PHASE_RESULTS } GamePhase;

static GameState *g_state = NULL;
static NetClient  g_client;
static GamePhase  g_phase = PHASE_LOBBY;

static void send_move(char direction)
{
    if (g_phase != PHASE_PLAYING || !g_client.has_map) return;
    net_client_send_move(&g_client, direction);
}

static void key_callback(GLFWwindow *win, int key, int scancode,
                         int action, int mods)
{
    (void)scancode;
    (void)mods;

    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(win, GLFW_TRUE);
        return;
    }

    if (g_phase == PHASE_LOBBY) {
        if (action == GLFW_PRESS &&
            (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER ||
             key == GLFW_KEY_SPACE)) {
            net_client_send_ready(&g_client);
        }
        return;
    }

    if (g_phase == PHASE_RESULTS) {
        if (action == GLFW_PRESS &&
            (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER ||
             key == GLFW_KEY_SPACE)) {
            uint8_t status = GAME_LOBBY;
            net_send_msg(g_client.fd, MSG_SET_STATUS, g_client.player_id,
                         TARGET_SERVER, &status, sizeof(status));
            g_phase = PHASE_LOBBY;
        }
        return;
    }

    switch (key) {
    case GLFW_KEY_UP:
    case GLFW_KEY_W:
        send_move('U');
        break;
    case GLFW_KEY_DOWN:
    case GLFW_KEY_S:
        send_move('D');
        break;
    case GLFW_KEY_LEFT:
    case GLFW_KEY_A:
        send_move('L');
        break;
    case GLFW_KEY_RIGHT:
    case GLFW_KEY_D:
        send_move('R');
        break;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
    case GLFW_KEY_SPACE:
        if (action == GLFW_PRESS && g_client.has_map)
            net_client_send_bomb(&g_client, g_state);
        break;
    default:
        break;
    }
}

static const char *status_name(GameStatus status)
{
    switch (status) {
    case GAME_LOBBY: return "lobby";
    case GAME_RUNNING: return "running";
    case GAME_END: return "end";
    default: return "unknown";
    }
}

int main(int argc, char *argv[])
{
    const char *player_name = (argc > 1) ? argv[1] : "DaBomb";
    const char *host = (argc > 2) ? argv[2] : NET_DEFAULT_HOST;
    int port = (argc > 3) ? atoi(argv[3]) : NET_PORT;
    if (port <= 0) port = NET_PORT;

    g_state = game_client_create();
    if (!g_state) {
        fprintf(stderr, "Failed to allocate client state\n");
        return 1;
    }

    if (net_client_connect(&g_client, host, port) != 0 ||
        net_client_hello(&g_client, player_name) != 0) {
        fprintf(stderr, "Failed to connect or join %s:%d\n", host, port);
        game_free(g_state);
        return 1;
    }

    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        net_client_close(&g_client);
        game_free(g_state);
        return 1;
    }

    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow *window = glfwCreateWindow(LOBBY_WIN_W, LOBBY_WIN_H,
                                          "Bomberman Client", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        net_client_close(&g_client);
        game_free(g_state);
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetKeyCallback(window, key_callback);
    render_init(15, 11);

    printf("Connected to %s:%d as player %u\n", host, port, g_client.player_id);
    printf("Lobby: Enter/Space = ready, Esc = quit\n");
    printf("Game:  WASD/Arrows = move attempt, Enter/Space = bomb attempt\n");

    int last_w = 0;
    int last_h = 0;
    char title[160];

    while (!glfwWindowShouldClose(window) && g_client.connected) {
        glfwPollEvents();

        if (net_client_poll(&g_client, g_state) < 0) {
            fprintf(stderr, "Server connection closed\n");
            break;
        }

        if (g_client.status == GAME_RUNNING && g_client.has_map && !g_state->game_over)
            g_phase = PHASE_PLAYING;
        else if (g_client.status == GAME_END || g_state->game_over)
            g_phase = PHASE_RESULTS;
        else
            g_phase = PHASE_LOBBY;

        if (g_client.has_map &&
            (last_w != g_state->map.width || last_h != g_state->map.height)) {
            last_w = g_state->map.width;
            last_h = g_state->map.height;
            glfwSetWindowSize(window, last_w * CELL_SIZE,
                              last_h * CELL_SIZE + HUD_HEIGHT);
        }

        if (g_client.has_map) {
            render_frame(g_state);
            if (g_phase == PHASE_RESULTS) {
                int winner = game_get_winner(g_state);
                char msg[32];
                if (winner == NO_WINNER)
                    snprintf(msg, sizeof(msg), "GAME OVER");
                else
                    snprintf(msg, sizeof(msg), "PLAYER %d WINS", winner + 1);
                render_game_over(g_state->map.width, g_state->map.height,
                                 winner, msg);
            }
        } else {
            glViewport(0, 0, LOBBY_WIN_W, LOBBY_WIN_H);
            glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        snprintf(title, sizeof(title), "Bomberman Client - P%u - %s%s",
                 g_client.player_id + 1, status_name(g_client.status),
                 g_client.has_map ? "" : " - waiting for map");
        glfwSetWindowTitle(window, title);
        glfwSwapBuffers(window);
    }

    render_cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    net_client_close(&g_client);
    game_free(g_state);
    return 0;
}

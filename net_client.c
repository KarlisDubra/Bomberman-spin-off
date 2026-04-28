#include "net.h"
#include "game.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <string.h>

#define CLIENT_IDENT "BombermanClient"

static void close_socket(int fd)
{
#ifdef _WIN32
    closesocket((SOCKET)fd);
#else
    close(fd);
#endif
}

static void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

static int send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *ptr = (const uint8_t *)buf;
    while (len > 0) {
        int n = send(fd, (const char *)ptr, (int)len, 0);
        if (n <= 0) return -1;
        ptr += n;
        len -= (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
    uint8_t *ptr = (uint8_t *)buf;
    while (len > 0) {
        int n = recv(fd, (char *)ptr, (int)len, 0);
        if (n <= 0) return -1;
        ptr += n;
        len -= (size_t)n;
    }
    return 0;
}

int net_send_msg(int fd, uint8_t msg_type, uint8_t sender_id,
                 uint8_t target_id, const void *payload, size_t payload_len)
{
    if (payload_len > MAX_PAYLOAD_SIZE) return -1;

    uint8_t header[3] = { msg_type, sender_id, target_id };
    if (send_all(fd, header, sizeof(header)) != 0) return -1;
    if (payload_len > 0 && payload != NULL)
        return send_all(fd, payload, payload_len);
    return 0;
}

int net_client_connect(NetClient *client, const char *host, int port)
{
    if (!client) return -1;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return -1;
    }
#endif
    memset(client, 0, sizeof(*client));
    client->fd = -1;
    client->player_id = TARGET_SERVER;
    client->status = GAME_LOBBY;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
#ifdef _WIN32
        WSACleanup();
#endif
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server address: %s\n", host);
        close_socket(fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close_socket(fd);
#ifdef _WIN32
        WSACleanup();
#endif
        return -1;
    }

    client->fd = fd;
    client->connected = true;
    return 0;
}

int net_client_hello(NetClient *client, const char *player_name)
{
    if (!client || client->fd < 0) return -1;

    uint8_t payload[CLIENT_ID_LEN + MAX_NAME_LEN];
    memset(payload, 0, sizeof(payload));
    strncpy((char *)payload, CLIENT_IDENT, CLIENT_ID_LEN);
    strncpy((char *)payload + CLIENT_ID_LEN, player_name, MAX_NAME_LEN);

    if (net_send_msg(client->fd, MSG_HELLO, TARGET_SERVER, TARGET_SERVER,
                     payload, sizeof(payload)) != 0)
        return -1;

    NetMessage msg;
    int received = 0;
    for (int tries = 0; tries < 3000; tries++) {
        received = net_recv_message(client->fd, &msg);
        if (received != 0) break;
        sleep_ms(10);
    }
    if (received <= 0) return -1;
    if (msg.header.msg_type == MSG_DISCONNECT) return -1;
    if (msg.header.msg_type != MSG_WELCOME) return -1;

    client->player_id = msg.header.sender_id;
    client->has_welcome = true;
    if (msg.payload_len >= CLIENT_ID_LEN + 1)
        client->status = (GameStatus)msg.payload[CLIENT_ID_LEN];

    return 0;
}

void net_client_close(NetClient *client)
{
    if (!client || client->fd < 0) return;
    net_send_msg(client->fd, MSG_LEAVE, client->player_id, TARGET_SERVER, NULL, 0);
    close_socket(client->fd);
    client->fd = -1;
    client->connected = false;
#ifdef _WIN32
    WSACleanup();
#endif
}

int net_client_send_ready(NetClient *client)
{
    if (!client || client->fd < 0 || !client->has_welcome) return -1;
    return net_send_msg(client->fd, MSG_SET_READY, client->player_id,
                        TARGET_SERVER, NULL, 0);
}

int net_client_send_move(NetClient *client, char direction)
{
    if (!client || client->fd < 0 || !client->has_welcome) return -1;
    uint8_t payload = (uint8_t)direction;
    return net_send_msg(client->fd, MSG_MOVE_ATTEMPT, client->player_id,
                        TARGET_SERVER, &payload, 1);
}

int net_client_send_bomb(NetClient *client, const GameState *state)
{
    if (!client || client->fd < 0 || !state || !client->has_welcome)
        return -1;
    if (client->player_id >= MAX_PLAYERS || state->map.width == 0)
        return -1;

    const Player *p = &state->players[client->player_id];
    uint16_t cell = (uint16_t)p->y * state->map.width + p->x;
    uint16_t be_cell = htons(cell);
    return net_send_msg(client->fd, MSG_BOMB_ATTEMPT, client->player_id,
                        TARGET_SERVER, &be_cell, sizeof(be_cell));
}

static int socket_has_data(int fd)
{
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    int r = select(fd + 1, &read_fds, NULL, NULL, &tv);
    if (r <= 0) return r;
    return FD_ISSET(fd, &read_fds) ? 1 : 0;
}

int net_recv_message(int fd, NetMessage *msg)
{
    if (!msg) return -1;
    memset(msg, 0, sizeof(*msg));

    int ready = socket_has_data(fd);
    if (ready <= 0) return ready;

    uint8_t header[3];
    if (recv_all(fd, header, sizeof(header)) != 0) return -1;
    msg->header.msg_type = header[0];
    msg->header.sender_id = header[1];
    msg->header.target_id = header[2];

    switch ((MsgType)msg->header.msg_type) {
    case MSG_HELLO:
        msg->payload_len = CLIENT_ID_LEN + MAX_NAME_LEN;
        break;
    case MSG_WELCOME: {
        size_t fixed = CLIENT_ID_LEN + 2;
        if (recv_all(fd, msg->payload, fixed) != 0) return -1;
        uint8_t others = msg->payload[CLIENT_ID_LEN + 1];
        size_t rest = (size_t)others * (1 + 1 + MAX_NAME_LEN);
        if (fixed + rest > MAX_PAYLOAD_SIZE) return -1;
        if (recv_all(fd, msg->payload + fixed, rest) != 0) return -1;
        msg->payload_len = fixed + rest;
        return 1;
    }
    case MSG_MAP: {
        if (recv_all(fd, msg->payload, 2) != 0) return -1;
        uint8_t rows = msg->payload[0];
        uint8_t cols = msg->payload[1];
        size_t cells = (size_t)rows * cols;
        if (2 + cells > MAX_PAYLOAD_SIZE) return -1;
        if (recv_all(fd, msg->payload + 2, cells) != 0) return -1;
        msg->payload_len = 2 + cells;
        return 1;
    }
    case MSG_SET_STATUS:
    case MSG_WINNER:
    case MSG_MOVE_ATTEMPT:
    case MSG_DEATH:
        msg->payload_len = 1;
        break;
    case MSG_BOMB_ATTEMPT:
    case MSG_EXPLOSION_END:
    case MSG_BLOCK_DESTROYED:
        msg->payload_len = 2;
        break;
    case MSG_MOVED:
    case MSG_BOMB:
    case MSG_EXPLOSION_START:
    case MSG_BONUS_AVAILABLE:
    case MSG_BONUS_RETRIEVED:
        msg->payload_len = 3;
        break;
    case MSG_PING:
    case MSG_PONG:
    case MSG_DISCONNECT:
    case MSG_LEAVE:
    case MSG_ERROR:
    case MSG_SET_READY:
    default:
        msg->payload_len = 0;
        break;
    }

    if (msg->payload_len > 0 &&
        recv_all(fd, msg->payload, msg->payload_len) != 0)
        return -1;
    return 1;
}

static uint16_t read_u16_be(const uint8_t *payload)
{
    uint16_t value;
    memcpy(&value, payload, sizeof(value));
    return ntohs(value);
}

static void apply_message(NetClient *client, GameState *state,
                          const NetMessage *msg)
{
    switch ((MsgType)msg->header.msg_type) {
    case MSG_DISCONNECT:
        client->connected = false;
        break;
    case MSG_PING:
        net_send_msg(client->fd, MSG_PONG, client->player_id, TARGET_SERVER, NULL, 0);
        break;
    case MSG_SET_STATUS:
        if (msg->payload_len >= 1) {
            client->status = (GameStatus)msg->payload[0];
            if (client->status == GAME_END)
                game_client_apply_winner(state, NO_WINNER);
        }
        break;
    case MSG_MAP:
        if (msg->payload_len >= 2 &&
            game_client_set_map(state, msg->payload[0], msg->payload[1],
                                msg->payload + 2) == 0) {
            client->has_map = true;
        }
        break;
    case MSG_MOVED:
        if (msg->payload_len >= 3)
            game_client_apply_moved(state, msg->payload[0],
                                    read_u16_be(msg->payload + 1));
        break;
    case MSG_BOMB:
        if (msg->payload_len >= 3)
            game_client_apply_bomb(state, msg->payload[0],
                                   read_u16_be(msg->payload + 1));
        break;
    case MSG_EXPLOSION_START:
        if (msg->payload_len >= 3)
            game_client_apply_explosion_start(state, msg->payload[0],
                                              read_u16_be(msg->payload + 1));
        break;
    case MSG_EXPLOSION_END:
        if (msg->payload_len >= 2)
            game_client_apply_explosion_end(state, read_u16_be(msg->payload));
        break;
    case MSG_DEATH:
        if (msg->payload_len >= 1)
            game_client_apply_death(state, msg->payload[0]);
        break;
    case MSG_BONUS_AVAILABLE:
        if (msg->payload_len >= 3)
            game_client_apply_bonus_available(state, msg->payload[0],
                                              read_u16_be(msg->payload + 1));
        break;
    case MSG_BONUS_RETRIEVED:
        if (msg->payload_len >= 3)
            game_client_apply_bonus_retrieved(state, msg->payload[0],
                                             read_u16_be(msg->payload + 1));
        break;
    case MSG_BLOCK_DESTROYED:
        if (msg->payload_len >= 2)
            game_client_apply_block_destroyed(state, read_u16_be(msg->payload));
        break;
    case MSG_WINNER:
        if (msg->payload_len >= 1)
            game_client_apply_winner(state, (int8_t)msg->payload[0]);
        break;
    default:
        break;
    }
}

int net_client_poll(NetClient *client, GameState *state)
{
    if (!client || client->fd < 0 || !state) return -1;

    int handled = 0;
    while (client->connected) {
        NetMessage msg;
        int r = net_recv_message(client->fd, &msg);
        if (r == 0) break;
        if (r < 0) {
            client->connected = false;
            return -1;
        }
        apply_message(client, state, &msg);
        handled++;
    }
    return handled;
}

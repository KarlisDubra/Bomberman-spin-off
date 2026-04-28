#ifndef NET_H
#define NET_H

#include "game.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NET_DEFAULT_HOST "127.0.0.1"
#define NET_PORT 4444

#define CLIENT_ID_LEN 20
#define MAX_NAME_LEN 30
#define MAX_PAYLOAD_SIZE (2 + MAX_MAP_WIDTH * MAX_MAP_HEIGHT)
#define TARGET_BROADCAST 254
#define TARGET_SERVER 255

typedef enum {
    GAME_LOBBY = 0,
    GAME_RUNNING = 1,
    GAME_END = 2
} GameStatus;

typedef enum {
    MSG_HELLO = 0,
    MSG_WELCOME = 1,
    MSG_DISCONNECT = 2,
    MSG_PING = 3,
    MSG_PONG = 4,
    MSG_LEAVE = 5,
    MSG_ERROR = 6,
    MSG_MAP = 7,
    MSG_SET_READY = 10,
    MSG_SET_STATUS = 20,
    MSG_WINNER = 23,
    MSG_MOVE_ATTEMPT = 30,
    MSG_BOMB_ATTEMPT = 31,
    MSG_MOVED = 40,
    MSG_BOMB = 41,
    MSG_EXPLOSION_START = 42,
    MSG_EXPLOSION_END = 43,
    MSG_DEATH = 44,
    MSG_BONUS_AVAILABLE = 45,
    MSG_BONUS_RETRIEVED = 46,
    MSG_BLOCK_DESTROYED = 47
} MsgType;

typedef struct {
    uint8_t msg_type;
    uint8_t sender_id;
    uint8_t target_id;
} MsgHeader;

typedef struct {
    MsgHeader header;
    uint8_t payload[MAX_PAYLOAD_SIZE];
    size_t payload_len;
} NetMessage;

typedef struct {
    int fd;
    uint8_t player_id;
    GameStatus status;
    bool connected;
    bool has_welcome;
    bool has_map;
} NetClient;

int  net_client_connect(NetClient *client, const char *host, int port);
int  net_client_hello(NetClient *client, const char *player_name);
void net_client_close(NetClient *client);

int  net_client_send_ready(NetClient *client);
int  net_client_send_move(NetClient *client, char direction);
int  net_client_send_bomb(NetClient *client, const GameState *state);

int  net_client_poll(NetClient *client, GameState *state);
int  net_recv_message(int fd, NetMessage *msg);
int  net_send_msg(int fd, uint8_t msg_type, uint8_t sender_id,
                  uint8_t target_id, const void *payload, size_t payload_len);

#endif /* NET_H */

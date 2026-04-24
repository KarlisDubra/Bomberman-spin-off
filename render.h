#ifndef RENDER_H
#define RENDER_H

#include "game.h"

#define CELL_SIZE      56   /* pixels per grid cell */
#define HUD_HEIGHT     60   /* pixels reserved below the map for player status */
#define MINIMAP_CELL   18   /* cell size in the lobby map preview */

/* Fixed window dimensions (lobby + random game both use 15×11 maps) */
#define LOBBY_WIN_W  (15 * CELL_SIZE)
#define LOBBY_WIN_H  (11 * CELL_SIZE + HUD_HEIGHT)

/*
 * Set up the OpenGL projection for a map of the given dimensions.
 * Call once after creating and making the GL context current.
 */
void render_init(int map_width, int map_height);

/*
 * Draw the full frame: map cells, explosions, bombs, players, HUD.
 * Call every frame inside the game loop.
 */
void render_frame(const GameState *state);

/*
 * Draw a semi-transparent "game over" overlay.
 * Call render_frame first, then this on top.
 */
void render_game_over(int map_width, int map_height, int winner);

/*
 * Draw the lobby: two mini-map previews with mode titles.
 * selected = 0 or 1.  Call every frame while in lobby phase.
 */
void render_lobby(int win_w, int win_h,
                  const Map maps[GAMEMODE_COUNT], int selected);

/* No-op for fixed-function GL — here for symmetry. */
void render_cleanup(void);

#endif /* RENDER_H */

#include "render.h"
#include <GLFW/glfw3.h>
#include <math.h>

#define PI 3.14159265f

/* ── Per-player colours ─────────────────────────────────────────────────── */

static const float PLAYER_COLORS[MAX_PLAYERS][3] = {
    {0.25f, 0.50f, 1.00f},  /* 0  blue   */
    {1.00f, 0.25f, 0.25f},  /* 1  red    */
    {0.20f, 0.85f, 0.30f},  /* 2  green  */
    {0.85f, 0.25f, 0.85f},  /* 3  purple */
    {1.00f, 0.85f, 0.10f},  /* 4  yellow */
    {0.10f, 0.85f, 0.85f},  /* 5  cyan   */
    {1.00f, 0.55f, 0.10f},  /* 6  orange */
    {0.85f, 0.85f, 0.85f},  /* 7  white  */
};

/* ── Primitive helpers ──────────────────────────────────────────────────── */

static void rect(float x, float y, float w, float h,
                 float r, float g, float b, float a)
{
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x,     y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x,     y + h);
    glEnd();
}

static void circle(float cx, float cy, float radius,
                   float r, float g, float b, float a)
{
    const int N = 28;
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= N; i++) {
        float ang = 2.0f * PI * (float)i / N;
        glVertex2f(cx + radius * cosf(ang), cy + radius * sinf(ang));
    }
    glEnd();
}

/* Letter M drawn with line segments, centered at (cx, cy) */
static void draw_M(float cx, float cy, float size, float r, float g, float b)
{
    float x1   = cx - size * 0.55f;
    float x2   = cx + size * 0.55f;
    float xm   = cx;
    float ytop = cy - size * 0.65f;
    float ybot = cy + size * 0.65f;
    float ymid = cy + size * 0.10f;

    glLineWidth(2.5f);
    glColor3f(r, g, b);
    glBegin(GL_LINES);
    glVertex2f(x1, ybot); glVertex2f(x1, ytop);
    glVertex2f(x1, ytop); glVertex2f(xm, ymid);
    glVertex2f(xm, ymid); glVertex2f(x2, ytop);
    glVertex2f(x2, ytop); glVertex2f(x2, ybot);
    glEnd();
    glLineWidth(1.5f);
}

/* Diamond (bonus item) */
static void diamond(float cx, float cy, float half,
                    float r, float g, float b)
{
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    glVertex2f(cx,        cy - half);
    glVertex2f(cx + half, cy);
    glVertex2f(cx,        cy + half);
    glVertex2f(cx - half, cy);
    glEnd();
}

/* Heart: two circles for lobes + triangle for bottom point */
static void draw_heart(float cx, float cy, float size,
                       float r, float g, float b, float alpha)
{
    float lobe_r = size * 0.50f;
    circle(cx - lobe_r * 0.75f, cy - size * 0.10f, lobe_r, r, g, b, alpha);
    circle(cx + lobe_r * 0.75f, cy - size * 0.10f, lobe_r, r, g, b, alpha);
    glColor4f(r, g, b, alpha);
    glBegin(GL_TRIANGLES);
    glVertex2f(cx - size * 1.10f, cy);
    glVertex2f(cx + size * 1.10f, cy);
    glVertex2f(cx,                cy + size * 1.30f);
    glEnd();
}

/*
 * 7-segment digit renderer.
 * Segments bitmask: bit0=top, bit1=top-right, bit2=bot-right,
 *                   bit3=bottom, bit4=bot-left, bit5=top-left, bit6=middle
 */
static const unsigned char SEG_DIGITS[10] = {
    0x3F, /* 0: 0b0111111 */
    0x06, /* 1: 0b0000110 */
    0x5B, /* 2: 0b1011011 */
    0x4F, /* 3: 0b1001111 */
    0x66, /* 4: 0b1100110 */
    0x6D, /* 5: 0b1101101 */
    0x7D, /* 6: 0b1111101 */
    0x07, /* 7: 0b0000111 */
    0x7F, /* 8: 0b1111111 */
    0x6F, /* 9: 0b1101111 */
};

static void draw_digit(float cx, float cy, float size,
                       int digit, float r, float g, float b)
{
    if (digit < 0 || digit > 9) return;
    unsigned char segs = SEG_DIGITS[digit];

    float hw = size * 0.38f;
    float hh = size * 0.50f;

    /* Segment endpoint pairs: x1, y1, x2, y2 */
    float seg[7][4] = {
        { cx-hw, cy-hh, cx+hw, cy-hh }, /* 0 top        */
        { cx+hw, cy-hh, cx+hw, cy    }, /* 1 top-right  */
        { cx+hw, cy,    cx+hw, cy+hh }, /* 2 bot-right  */
        { cx-hw, cy+hh, cx+hw, cy+hh }, /* 3 bottom     */
        { cx-hw, cy,    cx-hw, cy+hh }, /* 4 bot-left   */
        { cx-hw, cy-hh, cx-hw, cy    }, /* 5 top-left   */
        { cx-hw, cy,    cx+hw, cy    }, /* 6 middle     */
    };

    glLineWidth(2.2f);
    glColor4f(r, g, b, 1.0f);
    glBegin(GL_LINES);
    for (int i = 0; i < 7; i++) {
        if (segs & (1 << i)) {
            glVertex2f(seg[i][0], seg[i][1]);
            glVertex2f(seg[i][2], seg[i][3]);
        }
    }
    glEnd();
    glLineWidth(1.5f);
}

/* ── Bonus colour lookup ────────────────────────────────────────────────── */

/* Returns the RGB colour associated with each bonus type.
 * Default (SPEED/FUSE) is yellow; all others are distinct colours. */
static void bonus_color(BonusType bt, float *r, float *g, float *b)
{
    *r = 1.0f; *g = 1.0f; *b = 0.0f; /* SPEED / FUSE — yellow */
    switch (bt) {
    case BONUS_RADIUS: *r = 0.0f; *g = 0.9f; *b = 1.0f; break; /* cyan   */
    case BONUS_SHIELD: *r = 0.3f; *g = 0.7f; *b = 1.0f; break; /* blue   */
    case BONUS_KICK:   *r = 0.9f; *g = 0.3f; *b = 0.9f; break; /* purple */
    case BONUS_RAPID:  *r = 1.0f; *g = 0.1f; *b = 0.1f; break; /* red    */
    default: break;
    }
}

/* ── Coordinate helpers ─────────────────────────────────────────────────── */

static inline float cell_px(int x) { return (float)(x * CELL_SIZE); }
static inline float cell_py(int y) { return (float)(y * CELL_SIZE); }
static inline float cell_cx(int x) { return cell_px(x) + CELL_SIZE * 0.5f; }
static inline float cell_cy(int y) { return cell_py(y) + CELL_SIZE * 0.5f; }

/* ── Layer drawing functions ────────────────────────────────────────────── */

static void draw_cells(const GameState *s)
{
    const float PAD = 1.5f;
    const float SZ  = CELL_SIZE - 2.0f * PAD;

    for (int y = 0; y < s->map.height; y++) {
        for (int x = 0; x < s->map.width; x++) {
            float px = cell_px(x) + PAD;
            float py = cell_py(y) + PAD;
            CellType ct = s->map.cells[(size_t)y * s->map.width + x];

            switch (ct) {
            case CELL_HARD_WALL:
                rect(px, py, SZ, SZ, 0.16f, 0.16f, 0.20f, 1);
                rect(px,      py,      SZ,   2.0f, 0.30f, 0.30f, 0.36f, 1);
                rect(px,      py,      2.0f, SZ,   0.30f, 0.30f, 0.36f, 1);
                break;
            case CELL_SOFT_BLOCK:
                rect(px, py, SZ, SZ, 0.52f, 0.33f, 0.13f, 1);
                rect(px + SZ*0.30f, py,           2, SZ,  0.40f, 0.25f, 0.08f, 1);
                rect(px,            py + SZ*0.5f, SZ, 2,  0.40f, 0.25f, 0.08f, 1);
                break;
            case CELL_BONUS: {
                rect(px, py, SZ, SZ, 0.18f, 0.18f, 0.22f, 1);
                BonusType bt = s->map.bonus_types[(size_t)y * s->map.width + x];
                float dr, dg, db;
                bonus_color(bt, &dr, &dg, &db);
                diamond(cell_cx(x), cell_cy(y), SZ * 0.33f, dr, dg, db);
                break;
            }
            default:
                rect(px, py, SZ, SZ, 0.22f, 0.22f, 0.26f, 1);
                break;
            }
        }
    }
}

static void draw_explosions(const GameState *s)
{
    const float SZ = (float)CELL_SIZE;
    for (int i = 0; i < s->explosion_count; i++) {
        float px = cell_px(s->explosions[i].x);
        float py = cell_py(s->explosions[i].y);
        rect(px,      py,      SZ,       SZ,       1.0f, 0.50f, 0.00f, 0.92f);
        float in = SZ * 0.15f;
        rect(px + in, py + in, SZ-2*in, SZ-2*in,  1.0f, 0.92f, 0.40f, 1.0f);
    }
}

static void draw_bombs(const GameState *s)
{
    for (int i = 0; i < s->bomb_count; i++) {
        if (!s->bombs[i].active) continue;

        float cx = cell_cx(s->bombs[i].x);
        float cy = cell_cy(s->bombs[i].y);
        float R  = CELL_SIZE * 0.30f;

        circle(cx + 2, cy + 2, R, 0, 0, 0, 0.45f);
        float br = s->bombs[i].full_range ? 0.75f : 0.10f;
        float bg = s->bombs[i].full_range ? 0.05f : 0.10f;
        float bb = s->bombs[i].full_range ? 0.05f : 0.10f;
        circle(cx, cy, R, br, bg, bb, 1.0f);
        float gc = s->bombs[i].full_range ? 0.85f : 0.40f;
        circle(cx - R*0.28f, cy - R*0.28f, R*0.22f, gc, gc, gc, 1.0f);

        float frac = (float)s->bombs[i].ticks_remaining / (float)DEFAULT_FUSE_TICKS;
        frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
        circle(cx + R*0.55f, cy - R*0.65f, R*0.22f, 1.0f, frac, 0.0f, 1.0f);

        if (s->bombs[i].full_range)
            draw_M(cx, cy, R, 1.0f, 0.55f, 0.0f);
    }
}

static void draw_players(const GameState *s)
{
    for (int p = 0; p < s->player_count; p++) {
        if (!s->players[p].alive) continue;

        float cx = cell_cx(s->players[p].x);
        float cy = cell_cy(s->players[p].y);
        float R  = CELL_SIZE * 0.34f;
        float r  = PLAYER_COLORS[p][0];
        float g  = PLAYER_COLORS[p][1];
        float b  = PLAYER_COLORS[p][2];

        circle(cx + 2, cy + 3, R, 0, 0, 0, 0.40f);
        circle(cx, cy, R, r, g, b, 1.0f);
        float hr = fminf(r + 0.35f, 1.0f);
        float hg = fminf(g + 0.35f, 1.0f);
        float hb = fminf(b + 0.35f, 1.0f);
        circle(cx - R*0.28f, cy - R*0.28f, R*0.26f, hr, hg, hb, 1.0f);

        if (s->players[p].shielded) {
            float sr = R + 6.0f;
            glLineWidth(3.0f);
            glColor4f(0.40f, 0.80f, 1.0f, 0.90f);
            glBegin(GL_LINE_LOOP);
            for (int i = 0; i < 28; i++) {
                float ang = 2.0f * PI * (float)i / 28.0f;
                glVertex2f(cx + sr * cosf(ang), cy + sr * sinf(ang));
            }
            glEnd();
            glLineWidth(1.5f);
        }

        const int N = 28;
        glColor4f(0, 0, 0, 0.6f);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < N; i++) {
            float ang = 2.0f * PI * (float)i / N;
            glVertex2f(cx + R * cosf(ang), cy + R * sinf(ang));
        }
        glEnd();
    }
}

static void draw_hud(const GameState *s, int map_width, int map_height)
{
    float hud_y = (float)(map_height * CELL_SIZE);
    float win_w = (float)(map_width  * CELL_SIZE);

    /* background + separator */
    rect(0, hud_y, win_w, (float)HUD_HEIGHT, 0.08f, 0.08f, 0.10f, 1.0f);
    rect(0, hud_y, win_w, 2.0f,              0.40f, 0.40f, 0.50f, 1.0f);

    float slot_w = win_w / (float)s->player_count;
    float mid_y  = hud_y + HUD_HEIGHT * 0.5f;

    for (int p = 0; p < s->player_count; p++) {
        float sx    = p * slot_w;
        float r     = PLAYER_COLORS[p][0];
        float g     = PLAYER_COLORS[p][1];
        float b_col = PLAYER_COLORS[p][2];
        float alpha = s->players[p].alive ? 1.0f : 0.22f;

        /*
         * Slot layout:
         *
         *  [●]   ♥ ♥ ♥
         *        ● ● ●   (abilities)
         */

        float pcx    = sx + 24.0f;
        float info_x = sx + 58.0f;
        float top_y  = mid_y - 8.0f;
        float bot_y  = mid_y + 8.0f;

        circle(pcx, mid_y, 18.0f, r, g, b_col, alpha);

        /* ── Hearts: top-right row ── */
        float heart_size = 5.0f;
        for (int l = 0; l < s->players[p].lives; l++) {
            draw_heart(info_x + l * 14.0f, top_y,
                       heart_size, 0.95f, 0.15f, 0.20f, alpha);
        }

        if (s->players[p].alive) {
            /* circle outline */
            glColor4f(1, 1, 1, 0.5f);
            glBegin(GL_LINE_LOOP);
            for (int i = 0; i < 20; i++) {
                float ang = 2.0f * PI * (float)i / 20.0f;
                glVertex2f(pcx + 18.0f * cosf(ang), mid_y + 18.0f * sinf(ang));
            }
            glEnd();

            /* ── Ability dots: bottom-right row ── */
            float adot_x = info_x;
            if (s->players[p].shielded) {
                circle(adot_x, bot_y, 5.0f, 0.3f, 0.7f, 1.0f, 1.0f);
                adot_x += 14.0f;
            }
            if (s->players[p].can_kick) {
                circle(adot_x, bot_y, 5.0f, 0.9f, 0.3f, 0.9f, 1.0f);
                adot_x += 14.0f;
            }
            if (s->players[p].rapid_next) {
                circle(adot_x, bot_y, 5.0f, 1.0f, 0.1f, 0.1f, 1.0f);
            }

        } else {
            /* X mark over dead player */
            float hw = 10.0f;
            rect(pcx - hw, mid_y - 2.5f, hw * 2, 5.0f, 0.9f, 0.1f, 0.1f, 1.0f);
            rect(pcx - 2.5f, mid_y - hw, 5.0f, hw * 2, 0.9f, 0.1f, 0.1f, 1.0f);
        }
    }
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void render_init(int map_width, int map_height)
{
    (void)map_width;
    (void)map_height;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glLineWidth(1.5f);
}

void render_frame(const GameState *s)
{
    int win_w = s->map.width  * CELL_SIZE;
    int win_h = s->map.height * CELL_SIZE + HUD_HEIGHT;
    glViewport(0, 0, win_w, win_h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, win_w, win_h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    draw_cells(s);
    draw_explosions(s);
    draw_bombs(s);
    draw_players(s);
    draw_hud(s, s->map.width, s->map.height);
}

void render_game_over(int map_width, int map_height, int winner)
{
    float win_w = (float)(map_width  * CELL_SIZE);
    float win_h = (float)(map_height * CELL_SIZE);

    rect(0, 0, win_w, win_h, 0.0f, 0.0f, 0.0f, 0.55f);

    float r = 0.8f, g = 0.1f, b = 0.1f;
    if (winner >= 0 && winner < MAX_PLAYERS) {
        r = PLAYER_COLORS[winner][0];
        g = PLAYER_COLORS[winner][1];
        b = PLAYER_COLORS[winner][2];
    }
    float band_h = win_h * 0.18f;
    rect(0, (win_h - band_h) * 0.5f, win_w, band_h, r, g, b, 0.75f);
}

/* ── Stroke font ────────────────────────────────────────────────────────── */
/*
 * Each letter is defined as pairs of (x,y) endpoints in a normalised space
 * where the glyph is centred at (0,0), half-height = 1.0.
 * Actual pixel coords: px = cx + vx * half_h,  py = cy + vy * half_h
 */

typedef struct { float x1,y1,x2,y2; } Stk;

#define S(x1,y1,x2,y2) {x1,y1,x2,y2}

static const Stk G_B[]={S(-.35f,-.5f,-.35f,.5f),S(-.35f,-.5f,.10f,-.5f),
 S(.10f,-.5f,.38f,-.32f),S(.38f,-.32f,.38f,-.05f),S(.38f,-.05f,.10f,.0f),
 S(-.35f,.0f,.10f,.0f),
 S(.10f,.0f,.40f,.22f),S(.40f,.22f,.40f,.46f),S(.40f,.46f,.10f,.5f),
 S(-.35f,.5f,.10f,.5f)};
static const Stk G_G[]={S(.38f,-.30f,.18f,-.5f),S(.18f,-.5f,-.18f,-.5f),
 S(-.18f,-.5f,-.38f,-.30f),S(-.38f,-.30f,-.38f,.30f),
 S(-.38f,.30f,-.18f,.5f),S(-.18f,.5f,.18f,.5f),S(.18f,.5f,.38f,.30f),
 S(.38f,.30f,.38f,.0f),S(.38f,.0f,.05f,.0f)};
static const Stk G_I[]={S(-.25f,-.5f,.25f,-.5f),S(.0f,-.5f,.0f,.5f),
 S(-.25f,.5f,.25f,.5f)};
static const Stk G_L[]={S(-.15f,-.5f,-.15f,.5f),S(-.15f,.5f,.38f,.5f)};
static const Stk G_M[]={S(-.38f,.5f,-.38f,-.5f),S(-.38f,-.5f,.0f,.0f),
 S(.0f,.0f,.38f,-.5f),S(.38f,-.5f,.38f,.5f)};
static const Stk G_O[]={S(-.18f,-.5f,.18f,-.5f),S(.18f,-.5f,.38f,-.30f),
 S(.38f,-.30f,.38f,.30f),S(.38f,.30f,.18f,.5f),S(.18f,.5f,-.18f,.5f),
 S(-.18f,.5f,-.38f,.30f),S(-.38f,.30f,-.38f,-.30f),S(-.38f,-.30f,-.18f,-.5f)};
static const Stk G_T[]={S(-.40f,-.5f,.40f,-.5f),S(.0f,-.5f,.0f,.5f)};
static const Stk G_Y[]={S(-.38f,-.5f,.0f,.0f),S(.38f,-.5f,.0f,.0f),
 S(.0f,.0f,.0f,.5f)};

#undef S

static void draw_char(char c, float cx, float cy, float h,
                      float r, float g, float b)
{
    const Stk *st = NULL; int n = 0;
    switch (c) {
    case 'B': st=G_B; n=(int)(sizeof(G_B)/sizeof(G_B[0])); break;
    case 'G': st=G_G; n=(int)(sizeof(G_G)/sizeof(G_G[0])); break;
    case 'I': st=G_I; n=(int)(sizeof(G_I)/sizeof(G_I[0])); break;
    case 'L': st=G_L; n=(int)(sizeof(G_L)/sizeof(G_L[0])); break;
    case 'M': st=G_M; n=(int)(sizeof(G_M)/sizeof(G_M[0])); break;
    case 'O': st=G_O; n=(int)(sizeof(G_O)/sizeof(G_O[0])); break;
    case 'T': st=G_T; n=(int)(sizeof(G_T)/sizeof(G_T[0])); break;
    case 'Y': st=G_Y; n=(int)(sizeof(G_Y)/sizeof(G_Y[0])); break;
    default:  return; /* space and unknowns = skip */
    }
    float hh = h * 0.5f;
    glColor3f(r, g, b);
    glBegin(GL_LINES);
    for (int i = 0; i < n; i++) {
        glVertex2f(cx + st[i].x1 * hh, cy + st[i].y1 * hh);
        glVertex2f(cx + st[i].x2 * hh, cy + st[i].y2 * hh);
    }
    glEnd();
}

/* Draw a null-terminated uppercase string centred at (cx, cy). */
static void draw_string(const char *s, float cx, float cy, float h,
                        float r, float g, float b)
{
    int len = 0;
    for (const char *p = s; *p; p++) len++;
    float slot = h * 0.90f;   /* width per character slot */
    float total = len * slot;
    float x0 = cx - total * 0.5f + slot * 0.5f;
    glLineWidth(2.8f);
    for (int i = 0; i < len; i++)
        draw_char(s[i], x0 + i * slot, cy, h, r, g, b);
    glLineWidth(1.5f);
}

/* ── Mini-map renderer ──────────────────────────────────────────────────── */

static void draw_minimap(const Map *m, float ox, float oy, float cs)
{
    /* cs = cell size in pixels for this preview */
    for (int y = 0; y < m->height; y++) {
        for (int x = 0; x < m->width; x++) {
            float px = ox + x * cs;
            float py = oy + y * cs;
            CellType ct = m->cells[(size_t)y * m->width + x];
            switch (ct) {
            case CELL_HARD_WALL:
                rect(px, py, cs, cs, 0.16f, 0.16f, 0.20f, 1.0f);
                break;
            case CELL_SOFT_BLOCK:
                rect(px, py, cs, cs, 0.52f, 0.33f, 0.13f, 1.0f);
                break;
            case CELL_BONUS: {
                rect(px, py, cs, cs, 0.18f, 0.18f, 0.22f, 1.0f);
                BonusType bt = m->bonus_types[(size_t)y * m->width + x];
                float dr, dg, db;
                bonus_color(bt, &dr, &dg, &db);
                float hf = cs * 0.32f;
                diamond(px + cs*0.5f, py + cs*0.5f, hf, dr, dg, db);
                break;
            }
            default:
                rect(px, py, cs, cs, 0.22f, 0.22f, 0.26f, 1.0f);
                break;
            }
        }
    }
}

/* ── Lobby screen ───────────────────────────────────────────────────────── */

static const char *MODE_TITLE[GAMEMODE_COUNT]     = { "MOBILITY", "BIG BOOM" };
/* Accent colour per mode: MOBILITY = teal,  BIG BOOM = orange */
static const float MODE_COLOR[GAMEMODE_COUNT][3]  = {
    { 0.10f, 0.85f, 0.85f },   /* MOBILITY  */
    { 1.00f, 0.55f, 0.10f },   /* BIG BOOM  */
};
/* Bonus icons shown below each map title (3 per mode) */
static const BonusType MODE_ICONS[GAMEMODE_COUNT][3] = {
    { BONUS_SPEED, BONUS_KICK, BONUS_SHIELD },   /* MOBILITY */
    { BONUS_RAPID, BONUS_KICK, BONUS_RADIUS },   /* BIG BOOM */
};

void render_lobby(int win_w, int win_h,
                  const Map maps[GAMEMODE_COUNT], int selected)
{
    glViewport(0, 0, win_w, win_h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, win_w, win_h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.07f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Map preview dimensions */
    float cs    = (float)MINIMAP_CELL;
    float mw    = maps[0].width  * cs;
    float mh    = maps[0].height * cs;
    float gap   = ((float)win_w - 2.0f * mw) / 3.0f;

    float map_x[2] = { gap,         gap * 2.0f + mw };
    float map_y    = (float)win_h * 0.38f;

    for (int m = 0; m < GAMEMODE_COUNT; m++) {
        float mx   = map_x[m];
        float my   = map_y;
        float mcx  = mx + mw * 0.5f;
        float cr    = MODE_COLOR[m][0];
        float cg    = MODE_COLOR[m][1];
        float mode_b = MODE_COLOR[m][2];
        float sel   = (m == selected) ? 1.0f : 0.35f;

        /* Selection border (bright) or dim border */
        float bw = 4.0f;
        rect(mx - bw, my - bw, mw + bw*2, mh + bw*2,
             cr * sel, cg * sel, mode_b * sel, 1.0f);

        /* Mini-map */
        draw_minimap(&maps[m], mx, my, cs);

        /* Mode title above map */
        float title_y = my - 38.0f;
        draw_string(MODE_TITLE[m], mcx, title_y, 28.0f,
                    cr * sel, cg * sel, mode_b * sel);

        /* Bonus icon diamonds below title */
        float icon_y  = my - 12.0f;
        float icon_sp = 24.0f;
        float icon_x0 = mcx - icon_sp;
        for (int k = 0; k < 3; k++) {
            BonusType bt = MODE_ICONS[m][k];
            float dr, dg, icon_b;
            bonus_color(bt, &dr, &dg, &icon_b);
            diamond(icon_x0 + k * icon_sp, icon_y, 7.0f,
                    dr * sel, dg * sel, icon_b * sel);
        }

        /* "selected" arrow under the map */
        if (m == selected) {
            float ay = my + mh + 14.0f;
            glColor3f(cr, cg, mode_b);
            glBegin(GL_TRIANGLES);
            glVertex2f(mcx - 10.0f, ay);
            glVertex2f(mcx + 10.0f, ay);
            glVertex2f(mcx,         ay + 14.0f);
            glEnd();
        }
    }

    /* Bottom hint */
    float hy = (float)win_h - 22.0f;
    draw_string("MOBILITY", (float)win_w * 0.25f, hy, 14.0f,
                0.45f, 0.45f, 0.55f);
    draw_string("BIG BOOM", (float)win_w * 0.75f, hy, 14.0f,
                0.45f, 0.45f, 0.55f);
    /* tiny "ENTER TO PLAY" label re-uses existing digit renderer via arrow hint above */
}

void render_cleanup(void) { /* nothing to free */ }
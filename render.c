#include "render.h"
#include <GLFW/glfw3.h>
#include <math.h>

#define PI 3.14159265f

/* Number of line segments used to approximate circles */
#define CIRCLE_SEGS 28

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
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= CIRCLE_SEGS; i++) {
        float ang = 2.0f * PI * (float)i / CIRCLE_SEGS;
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

static void bonus_color(BonusType bt, float *r, float *g, float *b)
{
    switch (bt) {
    case BONUS_SHIELD: *r = 0.25f; *g = 0.55f; *b = 1.00f; break; /* blue  */
    case BONUS_KICK:   *r = 0.20f; *g = 1.00f; *b = 0.15f; break; /* lime  */
    case BONUS_MEGA:   *r = 1.00f; *g = 0.10f; *b = 0.10f; break; /* red   */
    default:           *r = 0.80f; *g = 0.80f; *b = 0.80f; break; /* grey  */
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
        rect(px,         py,         SZ,          SZ,          1.0f, 0.50f, 0.00f, 0.92f);
        float inset = SZ * 0.15f;
        rect(px + inset, py + inset, SZ-2*inset, SZ-2*inset,  1.0f, 0.92f, 0.40f, 1.0f);
    }
}

static void draw_bombs(const GameState *s)
{
    for (int i = 0; i < s->bomb_count; i++) {
        if (!s->bombs[i].active) continue;

        float cx     = cell_cx(s->bombs[i].x);
        float cy     = cell_cy(s->bombs[i].y);
        float radius = CELL_SIZE * 0.30f;

        /* Shadow */
        circle(cx + 2, cy + 2, radius, 0, 0, 0, 0.45f);

        /*
         * Body colour: full-range (rapid) bombs are dark red,
         * normal bombs are near-black.
         */
        float body_r = s->bombs[i].full_range ? 0.75f : 0.10f;
        float body_g = s->bombs[i].full_range ? 0.05f : 0.10f;
        float body_b = s->bombs[i].full_range ? 0.05f : 0.10f;
        circle(cx, cy, radius, body_r, body_g, body_b, 1.0f);

        /* Glint highlight in upper-left corner of the bomb */
        float glint = s->bombs[i].full_range ? 0.85f : 0.40f;
        circle(cx - radius*0.28f, cy - radius*0.28f, radius*0.22f, glint, glint, glint, 1.0f);

        /* Fuse indicator: red when fresh, shifts to yellow as time runs out */
        float fuse_fraction = (float)s->bombs[i].ticks_remaining / (float)DEFAULT_FUSE_TICKS;
        fuse_fraction = fuse_fraction < 0 ? 0 : fuse_fraction > 1 ? 1 : fuse_fraction;
        circle(cx + radius*0.55f, cy - radius*0.65f, radius*0.22f, 1.0f, fuse_fraction, 0.0f, 1.0f);

        /* "M" marker indicates a full-range (rapid) bomb */
        if (s->bombs[i].full_range)
            draw_M(cx, cy, radius, 1.0f, 0.55f, 0.0f);
    }
}

static void draw_players(const GameState *s)
{
    for (int i = 0; i < s->player_count; i++) {
        if (!s->players[i].alive) continue;

        float cx     = cell_cx(s->players[i].x);
        float cy     = cell_cy(s->players[i].y);
        float radius = CELL_SIZE * 0.34f;
        float r      = PLAYER_COLORS[i][0];
        float g      = PLAYER_COLORS[i][1];
        float b      = PLAYER_COLORS[i][2];

        /* Shadow, body, highlight */
        circle(cx + 2, cy + 3, radius, 0, 0, 0, 0.40f);
        circle(cx, cy, radius, r, g, b, 1.0f);
        float hi_r = fminf(r + 0.35f, 1.0f);
        float hi_g = fminf(g + 0.35f, 1.0f);
        float hi_b = fminf(b + 0.35f, 1.0f);
        circle(cx - radius*0.28f, cy - radius*0.28f, radius*0.26f, hi_r, hi_g, hi_b, 1.0f);

        /* Shield ring drawn around the player when shielded */
        if (s->players[i].shielded) {
            float shield_radius = radius + 6.0f;
            glLineWidth(3.0f);
            glColor4f(0.40f, 0.80f, 1.0f, 0.90f);
            glBegin(GL_LINE_LOOP);
            for (int seg = 0; seg < CIRCLE_SEGS; seg++) {
                float ang = 2.0f * PI * (float)seg / CIRCLE_SEGS;
                glVertex2f(cx + shield_radius * cosf(ang), cy + shield_radius * sinf(ang));
            }
            glEnd();
            glLineWidth(1.5f);
        }

        /* Black outline */
        glColor4f(0, 0, 0, 0.6f);
        glBegin(GL_LINE_LOOP);
        for (int seg = 0; seg < CIRCLE_SEGS; seg++) {
            float ang = 2.0f * PI * (float)seg / CIRCLE_SEGS;
            glVertex2f(cx + radius * cosf(ang), cy + radius * sinf(ang));
        }
        glEnd();
    }
}

static void draw_hud(const GameState *s, int map_width, int map_height)
{
    float hud_y = (float)(map_height * CELL_SIZE);
    float win_w = (float)(map_width  * CELL_SIZE);

    /* background + separator line */
    rect(0, hud_y, win_w, (float)HUD_HEIGHT, 0.08f, 0.08f, 0.10f, 1.0f);
    rect(0, hud_y, win_w, 2.0f,              0.40f, 0.40f, 0.50f, 1.0f);

    float slot_w = win_w / (float)s->player_count;
    float mid_y  = hud_y + HUD_HEIGHT * 0.5f;

    for (int i = 0; i < s->player_count; i++) {
        float slot_x = i * slot_w;
        float col_r  = PLAYER_COLORS[i][0];
        float col_g  = PLAYER_COLORS[i][1];
        float col_b  = PLAYER_COLORS[i][2];
        float alpha  = s->players[i].alive ? 1.0f : 0.22f;

        /*
         * Slot layout (left to right within each player's column):
         *
         *  [●]   ♥ ♥ ♥        ← portrait circle, then life hearts
         *        ● ● ●        ← active ability dots (shield / kick / rapid)
         */
        float portrait_cx = slot_x + 24.0f;
        float info_x      = slot_x + 58.0f;  /* x where hearts/dots start */
        float top_y       = mid_y - 8.0f;
        float bot_y       = mid_y + 8.0f;

        circle(portrait_cx, mid_y, 18.0f, col_r, col_g, col_b, alpha);

        /* Hearts: one per remaining life */
        float heart_size = 5.0f;
        for (int life_idx = 0; life_idx < s->players[i].lives; life_idx++) {
            draw_heart(info_x + life_idx * 14.0f, top_y,
                       heart_size, 0.95f, 0.15f, 0.20f, alpha);
        }

        if (s->players[i].alive) {
            /* Portrait outline ring */
            glColor4f(1, 1, 1, 0.5f);
            glBegin(GL_LINE_LOOP);
            for (int seg = 0; seg < 20; seg++) {
                float ang = 2.0f * PI * (float)seg / 20.0f;
                glVertex2f(portrait_cx + 18.0f * cosf(ang), mid_y + 18.0f * sinf(ang));
            }
            glEnd();

            /* Ability dots: one coloured dot per active ability */
            float ability_x = info_x;
            if (s->players[i].shielded) {
                circle(ability_x, bot_y, 5.0f, 0.3f, 0.7f, 1.0f, 1.0f);
                ability_x += 14.0f;
            }
            if (s->players[i].can_kick) {
                circle(ability_x, bot_y, 5.0f, 0.20f, 1.00f, 0.15f, 1.0f);
                ability_x += 14.0f;
            }
            if (s->players[i].mega_next) {
                circle(ability_x, bot_y, 5.0f, 1.0f, 0.1f, 0.1f, 1.0f);
            }

        } else {
            /* X mark over dead player's portrait */
            float cross_half = 10.0f;
            rect(portrait_cx - cross_half, mid_y - 2.5f, cross_half * 2, 5.0f, 0.9f, 0.1f, 0.1f, 1.0f);
            rect(portrait_cx - 2.5f, mid_y - cross_half, 5.0f, cross_half * 2, 0.9f, 0.1f, 0.1f, 1.0f);
        }
    }
}

/* Forward declaration — defined after render_game_over in this file */
static void draw_string(const char *text, float cx, float cy, float h,
                        float r, float g, float b);

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

void render_game_over(int map_width, int map_height, int winner, const char *msg)
{
    float win_w = (float)(map_width  * CELL_SIZE);
    float win_h = (float)(map_height * CELL_SIZE);

    rect(0, 0, win_w, win_h, 0.0f, 0.0f, 0.0f, 0.55f);

    float win_r = 0.8f, win_g = 0.1f, win_b = 0.1f;
    if (winner >= 0 && winner < MAX_PLAYERS) {
        win_r = PLAYER_COLORS[winner][0];
        win_g = PLAYER_COLORS[winner][1];
        win_b = PLAYER_COLORS[winner][2];
    }
    float band_h = win_h * 0.24f;
    float band_y = (win_h - band_h) * 0.5f;
    rect(0, band_y, win_w, band_h, win_r, win_g, win_b, 0.78f);

    if (msg) {
        float text_h = band_h * 0.42f;
        draw_string(msg, win_w * 0.5f, band_y + band_h * 0.5f, text_h,
                    1.0f, 1.0f, 1.0f);
    }
}

/* ── Stroke font ────────────────────────────────────────────────────────── */
/*
 * Each letter is defined as pairs of (x,y) endpoints in a normalised space
 * where the glyph is centred at (0,0), half-height = 1.0.
 * Actual pixel coords: px = cx + vx * half_h,  py = cy + vy * half_h
 */

typedef struct { float x1,y1,x2,y2; } Stk;

#define S(x1,y1,x2,y2) {x1,y1,x2,y2}

static const Stk G_A[]={S(-.35f,.5f,.0f,-.5f),S(.35f,.5f,.0f,-.5f),
 S(-.18f,.12f,.18f,.12f)};
static const Stk G_B[]={S(-.35f,-.5f,-.35f,.5f),S(-.35f,-.5f,.10f,-.5f),
 S(.10f,-.5f,.38f,-.32f),S(.38f,-.32f,.38f,-.05f),S(.38f,-.05f,.10f,.0f),
 S(-.35f,.0f,.10f,.0f),
 S(.10f,.0f,.40f,.22f),S(.40f,.22f,.40f,.46f),S(.40f,.46f,.10f,.5f),
 S(-.35f,.5f,.10f,.5f)};
static const Stk G_C[]={S(.32f,-.30f,.15f,-.5f),S(.15f,-.5f,-.15f,-.5f),
 S(-.15f,-.5f,-.38f,-.28f),S(-.38f,-.28f,-.38f,.28f),
 S(-.38f,.28f,-.15f,.5f),S(-.15f,.5f,.15f,.5f),S(.15f,.5f,.32f,.30f)};
static const Stk G_D[]={S(-.32f,-.5f,-.32f,.5f),S(-.32f,-.5f,.10f,-.5f),
 S(.10f,-.5f,.38f,-.28f),S(.38f,-.28f,.38f,.28f),
 S(.38f,.28f,.10f,.5f),S(.10f,.5f,-.32f,.5f)};
static const Stk G_E[]={S(-.32f,-.5f,-.32f,.5f),S(-.32f,-.5f,.35f,-.5f),
 S(-.32f,.0f,.25f,.0f),S(-.32f,.5f,.35f,.5f)};
static const Stk G_G[]={S(.38f,-.30f,.18f,-.5f),S(.18f,-.5f,-.18f,-.5f),
 S(-.18f,-.5f,-.38f,-.30f),S(-.38f,-.30f,-.38f,.30f),
 S(-.38f,.30f,-.18f,.5f),S(-.18f,.5f,.18f,.5f),S(.18f,.5f,.38f,.30f),
 S(.38f,.30f,.38f,.0f),S(.38f,.0f,.05f,.0f)};
static const Stk G_H[]={S(-.35f,-.5f,-.35f,.5f),S(.35f,-.5f,.35f,.5f),
 S(-.35f,.0f,.35f,.0f)};
static const Stk G_I[]={S(-.25f,-.5f,.25f,-.5f),S(.0f,-.5f,.0f,.5f),
 S(-.25f,.5f,.25f,.5f)};
static const Stk G_K[]={S(-.32f,-.5f,-.32f,.5f),S(-.32f,.0f,.35f,-.5f),
 S(-.32f,.0f,.35f,.5f)};
static const Stk G_L[]={S(-.15f,-.5f,-.15f,.5f),S(-.15f,.5f,.38f,.5f)};
static const Stk G_M[]={S(-.38f,.5f,-.38f,-.5f),S(-.38f,-.5f,.0f,.0f),
 S(.0f,.0f,.38f,-.5f),S(.38f,-.5f,.38f,.5f)};
static const Stk G_N[]={S(-.35f,.5f,-.35f,-.5f),S(-.35f,-.5f,.35f,.5f),
 S(.35f,.5f,.35f,-.5f)};
static const Stk G_O[]={S(-.18f,-.5f,.18f,-.5f),S(.18f,-.5f,.38f,-.30f),
 S(.38f,-.30f,.38f,.30f),S(.38f,.30f,.18f,.5f),S(.18f,.5f,-.18f,.5f),
 S(-.18f,.5f,-.38f,.30f),S(-.38f,.30f,-.38f,-.30f),S(-.38f,-.30f,-.18f,-.5f)};
static const Stk G_P[]={S(-.32f,.5f,-.32f,-.5f),S(-.32f,-.5f,.10f,-.5f),
 S(.10f,-.5f,.38f,-.28f),S(.38f,-.28f,.38f,.0f),
 S(.38f,.0f,.10f,.0f),S(.10f,.0f,-.32f,.0f)};
static const Stk G_R[]={S(-.32f,.5f,-.32f,-.5f),S(-.32f,-.5f,.10f,-.5f),
 S(.10f,-.5f,.38f,-.28f),S(.38f,-.28f,.38f,.0f),
 S(.38f,.0f,.10f,.0f),S(.10f,.0f,-.32f,.0f),S(.10f,.0f,.38f,.5f)};
static const Stk G_S[]={S(.32f,-.5f,-.10f,-.5f),S(-.10f,-.5f,-.35f,-.28f),
 S(-.35f,-.28f,-.35f,.0f),S(-.35f,.0f,.35f,.0f),
 S(.35f,.0f,.35f,.28f),S(.35f,.28f,.10f,.5f),S(.10f,.5f,-.32f,.5f)};
static const Stk G_T[]={S(-.40f,-.5f,.40f,-.5f),S(.0f,-.5f,.0f,.5f)};
static const Stk G_W[]={S(-.38f,-.5f,-.20f,.5f),S(-.20f,.5f,.0f,.0f),
 S(.0f,.0f,.20f,.5f),S(.20f,.5f,.38f,-.5f)};
static const Stk G_Y[]={S(-.38f,-.5f,.0f,.0f),S(.38f,-.5f,.0f,.0f),
 S(.0f,.0f,.0f,.5f)};

#undef S

static void draw_char(char c, float cx, float cy, float h,
                      float r, float g, float b)
{
    if (c >= '0' && c <= '9') {
        draw_digit(cx, cy, h, c - '0', r, g, b);
        return;
    }
    const Stk *strokes = NULL;
    int stroke_count = 0;
#define SC(ch,letter) case ch: strokes=G_##letter; stroke_count=(int)(sizeof(G_##letter)/sizeof(G_##letter[0])); break
    switch (c) {
    SC('A',A); SC('B',B); SC('C',C); SC('D',D); SC('E',E);
    SC('G',G); SC('H',H); SC('I',I); SC('K',K); SC('L',L);
    SC('M',M); SC('N',N); SC('O',O); SC('P',P); SC('R',R);
    SC('S',S); SC('T',T); SC('W',W); SC('Y',Y);
    default: return; /* space and unknowns = skip */
    }
#undef SC
    float half_height = h * 0.5f;
    glColor3f(r, g, b);
    glBegin(GL_LINES);
    for (int i = 0; i < stroke_count; i++) {
        glVertex2f(cx + strokes[i].x1 * half_height, cy + strokes[i].y1 * half_height);
        glVertex2f(cx + strokes[i].x2 * half_height, cy + strokes[i].y2 * half_height);
    }
    glEnd();
}

/* Draw a null-terminated uppercase string centred at (cx, cy). */
static void draw_string(const char *text, float cx, float cy, float h,
                        float r, float g, float b)
{
    int len = 0;
    for (const char *ch = text; *ch; ch++) len++;
    float slot    = h * 0.90f;  /* pixel width reserved per character */
    float total   = len * slot;
    float start_x = cx - total * 0.5f + slot * 0.5f;
    glLineWidth(2.8f);
    for (int i = 0; i < len; i++)
        draw_char(text[i], start_x + i * slot, cy, h, r, g, b);
    glLineWidth(1.5f);
}

/* ── Mini-map renderer ──────────────────────────────────────────────────── */

static void draw_minimap(const Map *map, float ox, float oy, float cell_size)
{
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            float px = ox + x * cell_size;
            float py = oy + y * cell_size;
            CellType ct = map->cells[(size_t)y * map->width + x];
            switch (ct) {
            case CELL_HARD_WALL:
                rect(px, py, cell_size, cell_size, 0.16f, 0.16f, 0.20f, 1.0f);
                break;
            case CELL_SOFT_BLOCK:
                rect(px, py, cell_size, cell_size, 0.52f, 0.33f, 0.13f, 1.0f);
                break;
            case CELL_BONUS: {
                rect(px, py, cell_size, cell_size, 0.18f, 0.18f, 0.22f, 1.0f);
                BonusType bt = map->bonus_types[(size_t)y * map->width + x];
                float dr, dg, db;
                bonus_color(bt, &dr, &dg, &db);
                float diamond_half = cell_size * 0.32f;
                diamond(px + cell_size*0.5f, py + cell_size*0.5f, diamond_half, dr, dg, db);
                break;
            }
            default:
                rect(px, py, cell_size, cell_size, 0.22f, 0.22f, 0.26f, 1.0f);
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
/* All modes share the same three bonuses — map difference is layout/density */
static const BonusType MODE_ICONS[GAMEMODE_COUNT][3] = {
    { BONUS_SHIELD, BONUS_KICK, BONUS_MEGA },
    { BONUS_SHIELD, BONUS_KICK, BONUS_MEGA },
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

    /* Map preview pixel dimensions */
    float cell_size = (float)MINIMAP_CELL;
    float preview_w = maps[0].width  * cell_size;
    float preview_h = maps[0].height * cell_size;
    float gap       = ((float)win_w - 2.0f * preview_w) / 3.0f;

    float map_origin_x[2] = { gap,         gap * 2.0f + preview_w };
    float map_origin_y    = (float)win_h * 0.38f;

    for (int mode_idx = 0; mode_idx < GAMEMODE_COUNT; mode_idx++) {
        float preview_x  = map_origin_x[mode_idx];
        float preview_y  = map_origin_y;
        float map_center_x = preview_x + preview_w * 0.5f;
        float accent_r   = MODE_COLOR[mode_idx][0];
        float accent_g   = MODE_COLOR[mode_idx][1];
        float accent_b   = MODE_COLOR[mode_idx][2];
        /* brightness scales colour: 1.0 = selected (full), 0.35 = dim */
        float brightness = (mode_idx == selected) ? 1.0f : 0.35f;

        /* Selection border */
        float border_w = 4.0f;
        rect(preview_x - border_w, preview_y - border_w,
             preview_w + border_w*2, preview_h + border_w*2,
             accent_r * brightness, accent_g * brightness, accent_b * brightness, 1.0f);

        /* Mini-map */
        draw_minimap(&maps[mode_idx], preview_x, preview_y, cell_size);

        /* Mode title above map */
        float title_y = preview_y - 38.0f;
        draw_string(MODE_TITLE[mode_idx], map_center_x, title_y, 28.0f,
                    accent_r * brightness, accent_g * brightness, accent_b * brightness);

        /* Three bonus icon diamonds below the title */
        float icon_y      = preview_y - 12.0f;
        float icon_spacing = 24.0f;
        float icon_start_x = map_center_x - icon_spacing;
        for (int k = 0; k < 3; k++) {
            BonusType bt = MODE_ICONS[mode_idx][k];
            float dr, dg, icon_b;
            bonus_color(bt, &dr, &dg, &icon_b);
            diamond(icon_start_x + k * icon_spacing, icon_y, 7.0f,
                    dr * brightness, dg * brightness, icon_b * brightness);
        }

        /* Down-pointing arrow under the selected map */
        if (mode_idx == selected) {
            float arrow_y = preview_y + preview_h + 14.0f;
            glColor3f(accent_r, accent_g, accent_b);
            glBegin(GL_TRIANGLES);
            glVertex2f(map_center_x - 10.0f, arrow_y);
            glVertex2f(map_center_x + 10.0f, arrow_y);
            glVertex2f(map_center_x,         arrow_y + 14.0f);
            glEnd();
        }
    }

    /* ── Booster legend ──────────────────────────────────────────────────── */

    /* Thin separator line */
    float sep_y = (float)win_h * 0.725f;
    rect(win_w * 0.08f, sep_y, win_w * 0.84f, 1.0f, 0.35f, 0.35f, 0.45f, 1.0f);

    /* Three bonus entries: coloured diamond + label, evenly spaced */
    static const BonusType LEGEND_BONUS[3]  = { BONUS_SHIELD, BONUS_KICK, BONUS_MEGA };
    static const char     *LEGEND_LABEL[3]  = { "SHIELD",     "KICK",     "BLAST"    };
    static const char     *LEGEND_DESC[3]   = { "ABSORBS ONE HIT",
                                                "KICK BOMBS",
                                                "BREAKS WALLS" };
    float legend_y   = (float)win_h * 0.785f;
    float desc_y     = legend_y + 22.0f;
    float slots[3]   = { (float)win_w * 0.22f,
                         (float)win_w * 0.50f,
                         (float)win_w * 0.78f };
    for (int k = 0; k < 3; k++) {
        float dr, dg, db;
        bonus_color(LEGEND_BONUS[k], &dr, &dg, &db);
        diamond(slots[k] - 36.0f, legend_y, 8.0f, dr, dg, db);
        draw_string(LEGEND_LABEL[k], slots[k] + 16.0f, legend_y, 16.0f,
                    dr, dg, db);
        draw_string(LEGEND_DESC[k], slots[k], desc_y, 11.0f,
                    0.52f, 0.52f, 0.60f);
    }

    /* Mode name hints at the bottom of the screen */
    float hint_y = (float)win_h - 18.0f;
    draw_string("MOBILITY", (float)win_w * 0.25f, hint_y, 13.0f,
                0.40f, 0.40f, 0.50f);
    draw_string("BIG BOOM", (float)win_w * 0.75f, hint_y, 13.0f,
                0.40f, 0.40f, 0.50f);
}

void render_cleanup(void) { /* nothing to free */ }
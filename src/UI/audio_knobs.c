/*
 * audio_knobs.c
 *
 * Implementation of the virtual-knob widget declared in audio_knobs.h.
 * See that header for the public interface and usage notes.
 */

#include "audio_knobs.h"

#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- drawing helpers -------------------------------------------------- */

static void draw_circle(SDL_Renderer *r, int cx, int cy, int radius) {
    /* Midpoint circle algorithm, outline only. */
    int x = radius, y = 0, err = 0;
    while (x >= y) {
        SDL_RenderDrawPoint(r, cx + x, cy + y);
        SDL_RenderDrawPoint(r, cx + y, cy + x);
        SDL_RenderDrawPoint(r, cx - y, cy + x);
        SDL_RenderDrawPoint(r, cx - x, cy + y);
        SDL_RenderDrawPoint(r, cx - x, cy - y);
        SDL_RenderDrawPoint(r, cx - y, cy - x);
        SDL_RenderDrawPoint(r, cx + y, cy - x);
        SDL_RenderDrawPoint(r, cx + x, cy - y);
        if (err <= 0) { y += 1; err += 2 * y + 1; }
        if (err > 0)  { x -= 1; err -= 2 * x + 1; }
    }
}

/* Draws a filled arc from KNOB_MIN_ANGLE_DEG up to the angle
 * corresponding to the knob's current value, used as a "fill level"
 * indicator around the knob body. */
static void draw_value_arc(SDL_Renderer *r, const Knob *k) {
    float t = (k->value - k->min) / (k->max - k->min);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    double angle_deg = KNOB_MIN_ANGLE_DEG + t * (KNOB_MAX_ANGLE_DEG - KNOB_MIN_ANGLE_DEG);

    int steps = (int)(fabs(angle_deg - KNOB_MIN_ANGLE_DEG)) + 1;
    int inner = k->radius - 6;
    for (int i = 0; i <= steps; i++) {
        double a = (KNOB_MIN_ANGLE_DEG + (angle_deg - KNOB_MIN_ANGLE_DEG) * i / (steps ? steps : 1) - 90.0) * M_PI / 180.0;
        int x = k->cx + (int)(cos(a) * inner);
        int y = k->cy + (int)(sin(a) * inner);
        SDL_RenderDrawPoint(r, x, y);
        SDL_RenderDrawPoint(r, x + 1, y);
        SDL_RenderDrawPoint(r, x, y + 1);
    }
}

/* Draws the pointer/indicator line showing current rotation. */
static void draw_pointer(SDL_Renderer *r, const Knob *k) {
    float t = (k->value - k->min) / (k->max - k->min);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    double angle_deg = KNOB_MIN_ANGLE_DEG + t * (KNOB_MAX_ANGLE_DEG - KNOB_MIN_ANGLE_DEG);
    double a = (angle_deg - 90.0) * M_PI / 180.0; /* -90 so 0 deg points up */

    int x2 = k->cx + (int)(cos(a) * (k->radius - 10));
    int y2 = k->cy + (int)(sin(a) * (k->radius - 10));
    SDL_RenderDrawLine(r, k->cx, k->cy, x2, y2);
    SDL_RenderDrawLine(r, k->cx + 1, k->cy, x2 + 1, y2);
}

SDL_Texture *create_label_texture(SDL_Renderer *r, TTF_Font *font,
                                   const char *text, SDL_Color color,
                                   int *out_w, int *out_h) {
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) {
        fprintf(stderr, "TTF_RenderUTF8_Blended failed: %s\n", TTF_GetError());
        return NULL;
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    if (out_w) *out_w = surf->w;
    if (out_h) *out_h = surf->h;
    SDL_FreeSurface(surf);
    if (!tex) {
        fprintf(stderr, "SDL_CreateTextureFromSurface failed: %s\n", SDL_GetError());
    }
    return tex;
}

void draw_knob(SDL_Renderer *r, const Knob *k) {
    SDL_SetRenderDrawColor(r, 230, 230, 235, 255);
    draw_circle(r, k->cx, k->cy, k->radius);

    SDL_SetRenderDrawColor(r, 90, 170, 255, 255);
    draw_value_arc(r, k);

    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    draw_pointer(r, k);

    if (k->label_texture) {
        SDL_Rect dst = {
            k->cx - k->label_w / 2,
            k->cy + k->radius + 10,
            k->label_w,
            k->label_h
        };
        SDL_RenderCopy(r, k->label_texture, NULL, &dst);
    }
}

/* --- interaction -------------------------------------------------------- */

void handle_knob_event(Knob *k, const SDL_Event *e) {
    switch (e->type) {
    case SDL_MOUSEBUTTONDOWN: {
        int dx = e->button.x - k->cx;
        int dy = e->button.y - k->cy;
        if (dx * dx + dy * dy <= k->radius * k->radius) {
            k->dragging = true;
            k->drag_start_y = e->button.y;
            k->drag_start_value = k->value;
        }
        break;
    }
    case SDL_MOUSEBUTTONUP:
        k->dragging = false;
        break;
    case SDL_MOUSEMOTION:
        if (k->dragging) {
            int delta_y = k->drag_start_y - e->motion.y; /* up = positive */
            float range = k->max - k->min;
            float delta_value = (delta_y / DRAG_PIXELS_FOR_FULL_SWEEP) * range;
            float v = k->drag_start_value + delta_value;
            if (v < k->min) v = k->min;
            if (v > k->max) v = k->max;
            k->value = v;
        }
        break;
    default:
        break;
    }
}

/* --- font loading --------------------------------------------------------
 * Tries a font shipped next to the executable first, then falls back to
 * a common system font per platform. If none of these are found, the
 * app can still run — draw_knob() simply skips a label if its texture
 * is NULL — so a missing font is not fatal. */

TTF_Font *load_app_font(void) {
    static const char *candidates[] = {
        "font.ttf",
        "C:/Windows/Fonts/segoeui.ttf",                          /* Windows */
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",       /* Linux */
        "/System/Library/Fonts/Supplemental/Arial.ttf"           /* macOS */
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        TTF_Font *f = TTF_OpenFont(candidates[i], APP_FONT_SIZE);
        if (f) return f;
    }
    return NULL;
}

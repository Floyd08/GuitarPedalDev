/*
 * audio_knobs.h
 *
 * Reusable virtual-knob widget: drawing + mouse interaction + an
 * SDL_ttf label, for use in any SDL2 render loop. No main()/WinMain()
 * in here — see portaudio_knobs_demo.c for a full application that
 * uses this module.
 */
#ifndef AUDIO_KNOBS_H
#define AUDIO_KNOBS_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>

#define KNOB_RADIUS 60
#define APP_FONT_SIZE 16

/* Rotation sweep: knob goes from -135 deg to +135 deg (270 deg total),
 * matching the classic hardware-knob look. 0 deg = straight up. */
#define KNOB_MIN_ANGLE_DEG -135.0
#define KNOB_MAX_ANGLE_DEG  135.0

/* Vertical-drag knob behaviour: 150 px of drag = full range sweep. */
#define DRAG_PIXELS_FOR_FULL_SWEEP 150.0f

typedef struct {
    const char *label;
    int cx, cy;      /* center of the knob on screen */
    int radius;
    float min, max;  /* value range */
    float value;     /* current value */
    bool dragging;
    int drag_start_y;
    float drag_start_value;
    SDL_Texture *label_texture; /* rendered once via SDL_ttf, reused every frame */
    int label_w, label_h;
} Knob;

/* Draws the knob body, value arc, pointer and (if set) its label. */
void draw_knob(SDL_Renderer *r, const Knob *k);

/* Feed every SDL_Event through this for each knob to get mouse-drag
 * interaction. Safe to call for events unrelated to the knob. */
void handle_knob_event(Knob *k, const SDL_Event *e);

/* Renders `text` with the given font/color into a texture. Call this
 * once per label (e.g. at startup) and cache the result on the Knob —
 * don't call it every frame, text rendering is comparatively expensive.
 * Returns NULL (and logs to stderr) on failure; callers should treat
 * that as "skip drawing this label" rather than a fatal error. */
SDL_Texture *create_label_texture(SDL_Renderer *r, TTF_Font *font,
                                   const char *text, SDL_Color color,
                                   int *out_w, int *out_h);

/* Tries a font shipped next to the executable first (drop a file named
 * "font.ttf" there to control exactly what's used), then falls back to
 * a common system font per platform. Returns NULL if none are found;
 * callers should treat that as non-fatal (labels just won't be drawn). */
TTF_Font *load_app_font(void);

#endif /* AUDIO_KNOBS_H */

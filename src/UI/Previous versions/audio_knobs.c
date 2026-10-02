/*
 * audio_knobs.c
 *
 * Minimal, self-contained UI component: two virtual rotary knobs,
 * "Gain" and "Volume", for a PortAudio-style application.
 *
 * Dependencies: SDL2 only (no SDL2_gfx / SDL2_ttf required).
 *
 * Build (Linux/macOS):
 *   gcc audio_knobs.c -o audio_knobs `sdl2-config --cflags --libs` -lm
 *
 * Build (Windows, MSYS2/MinGW):
 *   gcc audio_knobs.c -o audio_knobs.exe -lSDL2main -lSDL2 -lm
 *
 * Interaction:
 *   Click and drag vertically on a knob to change its value.
 *   Dragging up increases the value, dragging down decreases it.
 *
 * Integration notes:
 *   - Knob.value is a plain float in [min,max]; read it each audio
 *     callback (e.g. gainKnob.value, volumeKnob.value) and apply it
 *     to your PortAudio stream (multiply samples, etc.).
 *   - draw_knob()/handle_knob_event() are independent of the rest of
 *     the demo, so you can lift the Knob struct and these two
 *     functions straight into an existing SDL2 render loop.
 */

/* Tell SDL we're providing the real entry point ourselves, so it
 * doesn't try to rename main() to SDL_main() behind the scenes.
 * (Sidesteps toolchain-specific quirks in that rename mechanism.) */
#define SDL_MAIN_HANDLED

#include <SDL2/SDL.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <stdbool.h>
#include <stdio.h>

#define WINDOW_W 480
#define WINDOW_H 300
#define KNOB_RADIUS 60

/* Rotation sweep: knob goes from -135 deg to +135 deg (270 deg total),
 * matching the classic hardware-knob look. 0 deg = straight up. */
#define KNOB_MIN_ANGLE_DEG -135.0
#define KNOB_MAX_ANGLE_DEG  135.0

typedef struct {
    const char *label;
    int cx, cy;      /* center of the knob on screen */
    int radius;
    float min, max;  /* value range */
    float value;     /* current value */
    bool dragging;
    int drag_start_y;
    float drag_start_value;
} Knob;

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

/* Extremely small 5x7 bitmap-free "label": we just draw a centered
 * placeholder rectangle strip since SDL2 alone has no font rendering.
 * Replace this with SDL_ttf text rendering in a real app; kept out
 * here to avoid pulling in another dependency for this demo. */
static void draw_label_tick(SDL_Renderer *r, const Knob *k) {
    SDL_Rect tick = { k->cx - 20, k->cy + k->radius + 10, 40, 4 };
    SDL_RenderFillRect(r, &tick);
}

static void draw_knob(SDL_Renderer *r, const Knob *k) {
    SDL_SetRenderDrawColor(r, 230, 230, 235, 255);
    draw_circle(r, k->cx, k->cy, k->radius);

    SDL_SetRenderDrawColor(r, 90, 170, 255, 255);
    draw_value_arc(r, k);

    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    draw_pointer(r, k);

    SDL_SetRenderDrawColor(r, 150, 150, 160, 255);
    draw_label_tick(r, k);
}

/* --- interaction -------------------------------------------------------
 * Vertical-drag knob behaviour: click inside the knob, then move the
 * mouse up/down to change value. 150 px of drag = full range sweep;
 * tune DRAG_PIXELS_FOR_FULL_SWEEP to taste. */
#define DRAG_PIXELS_FOR_FULL_SWEEP 150.0f

static void handle_knob_event(Knob *k, const SDL_Event *e) {
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

/* --- demo app ----------------------------------------------------------- */

static int app_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    SDL_SetMainReady(); /* required when SDL_MAIN_HANDLED is defined */

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "My Port Audio - Gain / Volume",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Knob gainKnob = {
        .label = "Gain", .cx = WINDOW_W / 2 - 100, .cy = WINDOW_H / 2,
        .radius = KNOB_RADIUS, .min = 0.0f, .max = 2.0f, .value = 1.0f
    };
    Knob volumeKnob = {
        .label = "Volume", .cx = WINDOW_W / 2 + 100, .cy = WINDOW_H / 2,
        .radius = KNOB_RADIUS, .min = 0.0f, .max = 1.0f, .value = 0.75f
    };

    bool running = true;
    SDL_Event e;
    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            handle_knob_event(&gainKnob, &e);
            handle_knob_event(&volumeKnob, &e);
        }

        /* This is where you'd push gainKnob.value / volumeKnob.value
         * into your PortAudio stream's processing state. */

        SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
        SDL_RenderClear(renderer);

        draw_knob(renderer, &gainKnob);
        draw_knob(renderer, &volumeKnob);

        SDL_RenderPresent(renderer);
        SDL_Delay(16); /* ~60 fps */
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

/* --- dual entry points ---------------------------------------------------
 * Some MinGW/MSYS2 toolchain setups end up expecting a WinMain() entry
 * point instead of (or in addition to) main(), depending on how the
 * default subsystem gets resolved at link time. Providing both here
 * means the program links correctly either way, without depending on
 * -mconsole/-mwindows being honoured a particular way on any given
 * machine. __argc/__argv are populated by the MinGW runtime before
 * either entry point is called. */

int main(int argc, char *argv[]) {
    return app_main(argc, argv);
}

#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return app_main(__argc, __argv);
}

#pragma once
#include <pebble.h>

typedef enum {
  ICON_CLOCK,
  ICON_FLAG,
  ICON_NOZZLE,
  ICON_BED,
  ICON_LAYERS,
  ICON_HOURGLASS,
  ICON_PHONE_OFF,
  ICON_REFRESH,
} IconId;

#define ICON_SIZE 12

void draw_bar(GContext *ctx, GRect rect, int permille, GColor fill, GColor track);
void draw_ring(GContext *ctx, GRect rect, int thickness, int permille, GColor fill, GColor track);
void draw_chip(GContext *ctx, GRect rect, uint8_t kind, bool highlighted);
void draw_icon(GContext *ctx, IconId icon, GPoint origin, GColor color);
void draw_state_glyph(GContext *ctx, uint8_t state, GPoint center, int size, GColor color);
void draw_printer_art(GContext *ctx, GPoint center, GColor frame, GColor accent);

// Text helpers. Text is only ever laid out in word-wrap mode and shortened
// with "…" by us: the firmware's ellipsis/fill layout can hang new PebbleOS
// builds when truncating (reproduced on the flint emulator).
int text_width(const char *text, GFont font);
void draw_text_fit(GContext *ctx, const char *text, GFont font, GRect box, GTextAlignment align, GColor color);
// Draws `text` on one line and returns the x just past it.
int draw_text_run(GContext *ctx, const char *text, GFont font, GColor color, int x, int y, int max_w);

void fmt_duration(char *buf, size_t size, int32_t seconds);
void fmt_clock(char *buf, size_t size, time_t when);
void fmt_since(char *buf, size_t size, time_t when);
void fmt_temp(char *buf, size_t size, int16_t current, int16_t target, bool with_target);

#pragma once
#include <pebble.h>

#define HEADER_HEIGHT 24

void ui_init(void);
void ui_deinit(void);
void ui_refresh(void);
void ui_apply_theme(void);
void ui_toast(const char *text, GColor accent);

// Shared top bar: optional brand chip (kind < 0 hides it), title, optional
// clock and phone/sync indicators.
void ui_draw_header(GContext *ctx, GRect rect, const char *title, GColor title_color, int kind, GColor bg,
                    bool show_clock);

void list_window_push(void);
void list_window_refresh(void);
void list_window_select(int index);
void list_window_apply_theme(void);
void list_window_destroy(void);

void detail_window_push(int index);
void detail_window_refresh(void);
void detail_window_destroy(void);

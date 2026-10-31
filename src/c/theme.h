#pragma once
#include <pebble.h>

typedef struct {
  GColor bg;         // list background
  GColor surface;    // detail background
  GColor card;
  GColor card_hl;
  GColor border;     // unselected card outline (GColorClear = none)
  GColor focus;      // selected card outline
  GColor text;
  GColor text_hl;
  GColor muted;
  GColor muted_hl;
  GColor track;      // empty part of bars and rings
  GColor track_hl;
  GColor toast;
  GColor toast_text;
  bool light;
} Theme;

void theme_apply(uint8_t which);
const Theme *theme(void);
GColor theme_state_color(uint8_t state, bool highlighted);
GColor theme_kind_color(uint8_t kind);
char theme_kind_letter(uint8_t kind);

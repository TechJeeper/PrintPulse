#include "ui.h"
#include "app.h"
#include "draw.h"
#include "model.h"
#include "theme.h"

#define TOAST_HEIGHT 34
#define TOAST_MS 4000

static Layer *s_toast_layer;
static AppTimer *s_toast_timer;
static char s_toast_text[48];
static GColor s_toast_accent;

void ui_init(void) {
  theme_apply(model_settings()->theme);
}

void ui_deinit(void) {
  if (s_toast_timer) {
    app_timer_cancel(s_toast_timer);
    s_toast_timer = NULL;
  }
  if (s_toast_layer) {
    layer_remove_from_parent(s_toast_layer);
    layer_destroy(s_toast_layer);
    s_toast_layer = NULL;
  }
  detail_window_destroy();
  list_window_destroy();
}

void ui_refresh(void) {
  list_window_refresh();
  detail_window_refresh();
}

void ui_apply_theme(void) {
  theme_apply(model_settings()->theme);
  list_window_apply_theme();
  ui_refresh();
}

void ui_draw_header(GContext *ctx, GRect r, const char *title, GColor title_color, int kind, GColor bg,
                    bool show_clock) {
  const Theme *th = theme();
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, r, 0, GCornerNone);

  int right = r.origin.x + r.size.w - 8;
  if (show_clock) {
    char clock[12];
    fmt_clock(clock, sizeof(clock), time(NULL));
    GFont clock_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    int clock_w = text_width(clock, clock_font);
    graphics_context_set_text_color(ctx, th->text);
    graphics_draw_text(ctx, clock, clock_font, GRect(right - 80, r.origin.y, 80, 24),
                       GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
    right -= clock_w + 6;
  }

  int mid_y = r.origin.y + r.size.h / 2;
  if (!app_is_connected()) {
    draw_icon(ctx, ICON_PHONE_OFF, GPoint(right - ICON_SIZE, mid_y - ICON_SIZE / 2),
              PBL_IF_COLOR_ELSE(GColorFolly, th->text));
    right -= ICON_SIZE + 4;
  } else if (app_is_syncing()) {
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(theme_state_color(PS_PRINTING, false), th->text));
    graphics_fill_circle(ctx, GPoint(right - 4, mid_y + 1), 3);
    right -= 12;
  }

  int x = r.origin.x + 8;
  if (kind >= 0) {
    draw_chip(ctx, GRect(x, mid_y - 8, 16, 16), kind, false);
    x += 22;
  }
  draw_text_fit(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(x, r.origin.y, right - x - 2, 24),
                GTextAlignmentLeft, title_color);
}

static void toast_update(Layer *layer, GContext *ctx) {
  const Theme *th = theme();
  GRect b = layer_get_bounds(layer);
  GRect card = GRect(6, 0, b.size.w - 12, b.size.h);
  graphics_context_set_fill_color(ctx, th->toast);
  graphics_fill_rect(ctx, card, 12, GCornersAll);
  graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(s_toast_accent, th->toast_text));
  graphics_fill_circle(ctx, GPoint(card.origin.x + 16, card.origin.y + card.size.h / 2), 5);
  draw_text_fit(ctx, s_toast_text, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                GRect(card.origin.x + 28, card.origin.y + 4, card.size.w - 34, 24), GTextAlignmentLeft,
                th->toast_text);
}

static void toast_hide(void *context) {
  s_toast_timer = NULL;
  if (s_toast_layer) {
    layer_set_hidden(s_toast_layer, true);
  }
}

void ui_toast(const char *text, GColor accent) {
  Window *top = window_stack_get_top_window();
  if (!top) {
    return;
  }
  strncpy(s_toast_text, text, sizeof(s_toast_text) - 1);
  s_toast_text[sizeof(s_toast_text) - 1] = '\0';
  s_toast_accent = accent;

  Layer *root = window_get_root_layer(top);
  GRect b = layer_get_bounds(root);
  if (!s_toast_layer) {
    s_toast_layer = layer_create(GRect(0, 0, b.size.w, TOAST_HEIGHT));
    layer_set_update_proc(s_toast_layer, toast_update);
  }
  layer_remove_from_parent(s_toast_layer);
  layer_set_frame(s_toast_layer, GRect(0, b.size.h - TOAST_HEIGHT - 6, b.size.w, TOAST_HEIGHT));
  layer_add_child(root, s_toast_layer);
  layer_set_hidden(s_toast_layer, false);
  layer_mark_dirty(s_toast_layer);

  if (s_toast_timer) {
    app_timer_reschedule(s_toast_timer, TOAST_MS);
  } else {
    s_toast_timer = app_timer_register(TOAST_MS, toast_hide, NULL);
  }
}

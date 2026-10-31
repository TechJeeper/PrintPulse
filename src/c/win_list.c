#include "app.h"
#include "comm.h"
#include "draw.h"
#include "model.h"
#include "theme.h"
#include "ui.h"

#define CARD_ROW_H 64
#define FOOTER_ROW_H 42

static Window *s_window;
static Layer *s_header;
static MenuLayer *s_menu;
static Layer *s_empty;

// ---- text helpers --------------------------------------------------------

static void summary(char *buf, size_t size, GColor *color) {
  int printing = 0, paused = 0, attention = 0, errors = 0, done = 0, n = model_count();
  for (int i = 0; i < n; i++) {
    switch (model_get(i)->state) {
      case PS_PRINTING:
      case PS_PREPARING: printing++; break;
      case PS_PAUSED: paused++; break;
      case PS_ATTENTION: attention++; break;
      case PS_ERROR: errors++; break;
      case PS_COMPLETE: done++; break;
      default: break;
    }
  }
  const Theme *th = theme();
  *color = th->text;
  if (n == 0) {
    snprintf(buf, size, "PrintPulse");
  } else if (errors + attention > 0) {
    snprintf(buf, size, "%d need%s you", errors + attention, errors + attention == 1 ? "s" : "");
    *color = theme_state_color(errors ? PS_ERROR : PS_ATTENTION, false);
  } else if (printing > 0) {
    snprintf(buf, size, "%d printing", printing);
    *color = theme_state_color(PS_PRINTING, false);
  } else if (paused > 0) {
    snprintf(buf, size, "%d paused", paused);
    *color = theme_state_color(PS_PAUSED, false);
  } else if (done > 0) {
    snprintf(buf, size, "%d done", done);
    *color = theme_state_color(PS_COMPLETE, false);
  } else {
    snprintf(buf, size, n == 1 ? "1 printer" : "%d printers", n);
  }
}

static void status_detail(const Printer *p, char *buf, size_t size, bool narrow) {
  const char *left_fmt = narrow ? "%s" : "%s left";
  char t[16];
  buf[0] = '\0';
  if (p->flags & PF_STALE) {
    fmt_clock(t, sizeof(t), p->updated);
    snprintf(buf, size, "seen %s", t);
    return;
  }
  switch (p->state) {
    case PS_PRINTING: {
      int32_t left = printer_time_left_now(p);
      if (left == 0) {
        snprintf(buf, size, "finishing");
      } else if (left > 0) {
        fmt_duration(t, sizeof(t), left);
        snprintf(buf, size, left_fmt, t);
      } else if (p->layer >= 0 && p->layer_total > 0) {
        snprintf(buf, size, "layer %d/%d", p->layer, p->layer_total);
      }
      break;
    }
    case PS_PAUSED:
    case PS_ATTENTION:
    case PS_PREPARING:
      if (p->message[0]) {
        snprintf(buf, size, "%s", p->message);
      } else if (p->time_left > 0) {
        fmt_duration(t, sizeof(t), p->time_left);
        snprintf(buf, size, left_fmt, t);
      }
      break;
    case PS_COMPLETE:
      if (p->elapsed > 0) {
        fmt_duration(t, sizeof(t), p->elapsed);
        snprintf(buf, size, "took %s", t);
      }
      break;
    default:
      break;
  }
}

// ---- rows ----------------------------------------------------------------

static void draw_card(GContext *ctx, GRect b, const Printer *p, bool hl) {
  const Theme *th = theme();
  GRect card = GRect(6, 3, b.size.w - 12, b.size.h - 6);
  graphics_context_set_fill_color(ctx, hl ? th->card_hl : th->card);
  graphics_fill_rect(ctx, card, 10, GCornersAll);
  GColor outline = hl ? th->focus : th->border;
  if (!gcolor_equal(outline, GColorClear)) {
    graphics_context_set_stroke_color(ctx, outline);
    graphics_context_set_stroke_width(ctx, hl ? 2 : 1);
    graphics_draw_round_rect(ctx, hl ? grect_inset(card, GEdgeInsets(1)) : card, 10);
    graphics_context_set_stroke_width(ctx, 1);
  }

  GColor text = hl ? th->text_hl : th->text;
  GColor muted = hl ? th->muted_hl : th->muted;
  GColor accent = theme_state_color(p->state, hl);
  if (p->state == PS_OFFLINE || p->state == PS_UNKNOWN) {
    accent = muted;
  }
  int x = card.origin.x, y = card.origin.y, w = card.size.w;
  // 144px-wide watches get a smaller chip so names have room.
  bool narrow = b.size.w < 180;
  int chip = narrow ? 16 : 22;
  draw_chip(ctx, GRect(x + (narrow ? 6 : 8), y + (narrow ? 6 : 8), chip, chip), p->kind, hl);

  // Name and percentage
  GFont bold18 = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont pct_font = narrow ? fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD) : bold18;
  bool has_progress = printer_shows_progress(p) && p->progress >= 0;
  int pct_w = 0;
  if (has_progress && p->state != PS_COMPLETE) {
    char pct[8];
    snprintf(pct, sizeof(pct), "%s%d%%", (p->flags & PF_ESTIMATED) ? "~" : "", p->progress / 10);
    pct_w = text_width(pct, pct_font) + 4;
    graphics_context_set_text_color(ctx, accent);
    graphics_draw_text(ctx, pct, pct_font, GRect(x + w - (narrow ? 6 : 8) - 60, y + (narrow ? 4 : 1), 60, 24),
                       GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
  }
  int tx = x + chip + (narrow ? 12 : 16);
  int tw = w - (tx - x) - (narrow ? 6 : 8);
  draw_text_fit(ctx, p->name, bold18, GRect(tx, y + 1, tw - pct_w, 24), GTextAlignmentLeft, text);

  // "Printing · 1h 20m left"
  char detail[MSG_LEN];
  status_detail(p, detail, sizeof(detail), narrow);
  int cx = draw_text_run(ctx, state_label(p->state), fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), accent,
                         tx, y + 21, tw);
  if (detail[0]) {
    char run[MSG_LEN + 4];
    snprintf(run, sizeof(run), " · %s", detail);
    draw_text_run(ctx, run, fonts_get_system_font(FONT_KEY_GOTHIC_14), muted, cx, y + 21, tx + tw - cx);
  }

  // Third line: progress bar, or temperatures / message
  if (has_progress) {
    draw_bar(ctx, GRect(x + 10, y + card.size.h - 13, w - 20, 6),
             p->state == PS_COMPLETE ? 1000 : p->progress, accent, hl ? th->track_hl : th->track);
    return;
  }
  char line[MSG_LEN + 8];
  if (p->state == PS_IDLE) {
    char n[16], bed[16];
    fmt_temp(n, sizeof(n), p->nozzle, p->nozzle_target, false);
    fmt_temp(bed, sizeof(bed), p->bed, p->bed_target, false);
    snprintf(line, sizeof(line), "Nozzle %s  ·  Bed %s", n, bed);
  } else {
    snprintf(line, sizeof(line), "%s", p->message[0] ? p->message : p->file);
  }
  draw_text_fit(ctx, line, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(x + 10, y + 37, w - 20, 18),
                GTextAlignmentLeft, muted);
}

static void draw_footer(GContext *ctx, GRect b, bool hl) {
  const Theme *th = theme();
  GRect pill = GRect(6, 4, b.size.w - 12, b.size.h - 8);
  graphics_context_set_fill_color(ctx, hl ? th->card_hl : th->bg);
  graphics_fill_rect(ctx, pill, pill.size.h / 2, GCornersAll);
  if (hl) {
    graphics_context_set_stroke_color(ctx, th->focus);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, grect_inset(pill, GEdgeInsets(1)), pill.size.h / 2 - 1);
    graphics_context_set_stroke_width(ctx, 1);
  }
  GColor text = hl ? th->text_hl : th->muted;
  int mid = pill.origin.y + pill.size.h / 2;
  draw_icon(ctx, ICON_REFRESH, GPoint(pill.origin.x + 12, mid - ICON_SIZE / 2), text);

  char when[24];
  if (app_is_syncing()) {
    snprintf(when, sizeof(when), "updating…");
  } else if (app_last_sync()) {
    fmt_since(when, sizeof(when), app_last_sync());
  } else {
    snprintf(when, sizeof(when), "not synced");
  }
  draw_text_fit(ctx, "Refresh", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                GRect(pill.origin.x + 30, mid - 10, 70, 18), GTextAlignmentLeft, text);
  draw_text_fit(ctx, when, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                GRect(pill.origin.x + 84, mid - 10, pill.size.w - 96, 18), GTextAlignmentRight, text);
}

// ---- menu callbacks ------------------------------------------------------

static uint16_t get_num_rows(MenuLayer *menu, uint16_t section, void *context) {
  int n = model_count();
  return n > 0 ? n + 1 : 0;
}

static int16_t get_cell_height(MenuLayer *menu, MenuIndex *index, void *context) {
  return index->row < model_count() ? CARD_ROW_H : FOOTER_ROW_H;
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *context) {
  GRect b = layer_get_bounds(cell);
  bool hl = menu_cell_layer_is_highlighted(cell);
  if (index->row < model_count()) {
    draw_card(ctx, b, model_get(index->row), hl);
  } else {
    draw_footer(ctx, b, hl);
  }
}

static void select_click(MenuLayer *menu, MenuIndex *index, void *context) {
  app_note_user_activity();
  if (index->row < model_count()) {
    detail_window_push(index->row);
  } else {
    comm_request_refresh();
  }
}

static void select_long_click(MenuLayer *menu, MenuIndex *index, void *context) {
  app_note_user_activity();
  comm_request_refresh();
}

static void selection_changed(MenuLayer *menu, MenuIndex new_index, MenuIndex old_index, void *context) {
  app_note_user_activity();
}

// ---- header and empty state ----------------------------------------------

static void header_update(Layer *layer, GContext *ctx) {
  char title[24];
  GColor color;
  summary(title, sizeof(title), &color);
  ui_draw_header(ctx, layer_get_bounds(layer), title, color, -1, theme()->bg, true);
}

static void empty_update(Layer *layer, GContext *ctx) {
  const Theme *th = theme();
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, th->bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  const char *title, *body;
  if (!app_has_synced() && !app_is_connected()) {
    title = "Phone not connected";
    body = "Connect your phone to load your printers.";
  } else if (!app_has_synced()) {
    title = "Connecting…";
    body = "Loading printers from your phone.";
  } else {
    title = "No printers yet";
    body = "Open the Pebble app on your phone, find PrintPulse and tap Settings to add one.";
  }
  bool compact = b.size.h < 170;
  int art_y = compact ? 34 : 46;
  draw_printer_art(ctx, GPoint(b.size.w / 2, art_y), th->muted, theme_state_color(PS_PRINTING, false));

  int y = art_y + 34;
  draw_text_fit(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GRect(8, y, b.size.w - 16, 30),
                GTextAlignmentCenter, th->text);
  graphics_context_set_text_color(ctx, th->muted);
  graphics_draw_text(ctx, body, fonts_get_system_font(compact ? FONT_KEY_GOTHIC_14 : FONT_KEY_GOTHIC_18),
                     GRect(10, y + 28, b.size.w - 20, b.size.h - y - 28), GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);
}

// ---- window --------------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  s_header = layer_create(GRect(0, 0, b.size.w, HEADER_HEIGHT));
  layer_set_update_proc(s_header, header_update);

  GRect body = GRect(0, HEADER_HEIGHT, b.size.w, b.size.h - HEADER_HEIGHT);
  s_menu = menu_layer_create(body);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = get_num_rows,
    .get_cell_height = get_cell_height,
    .draw_row = draw_row,
    .select_click = select_click,
    .select_long_click = select_long_click,
    .selection_changed = selection_changed,
  });
  menu_layer_set_click_config_onto_window(s_menu, window);

  s_empty = layer_create(body);
  layer_set_update_proc(s_empty, empty_update);

  layer_add_child(root, menu_layer_get_layer(s_menu));
  layer_add_child(root, s_empty);
  layer_add_child(root, s_header);
  list_window_apply_theme();
  list_window_refresh();
}

static void window_unload(Window *window) {
  menu_layer_destroy(s_menu);
  layer_destroy(s_empty);
  layer_destroy(s_header);
  s_menu = NULL;
  s_empty = NULL;
  s_header = NULL;
}

void list_window_push(void) {
  if (!s_window) {
    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
      .load = window_load,
      .unload = window_unload,
    });
  }
  window_stack_push(s_window, true);
}

void list_window_apply_theme(void) {
  if (!s_menu) {
    return;
  }
  const Theme *th = theme();
  window_set_background_color(s_window, th->bg);
  // Cards draw their own selection, so the stock highlight matches the background.
  menu_layer_set_normal_colors(s_menu, th->bg, th->text);
  menu_layer_set_highlight_colors(s_menu, th->bg, th->text);
}

void list_window_refresh(void) {
  if (!s_menu) {
    return;
  }
  bool empty = model_count() == 0;
  layer_set_hidden(s_empty, !empty);
  layer_set_hidden(menu_layer_get_layer(s_menu), empty);
  menu_layer_reload_data(s_menu);
  layer_mark_dirty(s_empty);
  layer_mark_dirty(s_header);
}

void list_window_select(int index) {
  if (!s_menu || index < 0 || index >= model_count()) {
    return;
  }
  menu_layer_set_selected_index(s_menu, MenuIndex(0, index), MenuRowAlignCenter, false);
}

void list_window_destroy(void) {
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}

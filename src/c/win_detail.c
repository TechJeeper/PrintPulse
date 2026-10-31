#include "app.h"
#include "comm.h"
#include "draw.h"
#include "model.h"
#include "theme.h"
#include "ui.h"

#define MAX_CELLS 6

typedef struct {
  IconId icon;
  char text[20];
} Cell;

static Window *s_window;
static Layer *s_canvas;
static int s_index;

static void fmt_eta(char *buf, size_t size, time_t eta) {
  time_t now = time(NULL);
  int today = localtime(&now)->tm_yday;
  struct tm *t = localtime(&eta);
  char clock[12];
  fmt_clock(clock, sizeof(clock), eta);
  if (t->tm_yday == today) {
    snprintf(buf, size, "%s", clock);
  } else {
    char day[6];
    strftime(day, sizeof(day), "%a", t);
    snprintf(buf, size, "%s %s", day, clock);
  }
}

static int add_cell(Cell *cells, int n, IconId icon, const char *text) {
  if (n >= MAX_CELLS) {
    return n;
  }
  cells[n].icon = icon;
  strncpy(cells[n].text, text, sizeof(cells[n].text) - 1);
  cells[n].text[sizeof(cells[n].text) - 1] = '\0';
  return n + 1;
}

static int build_cells(const Printer *p, Cell *cells, bool compact) {
  char t[20];
  int n = 0;
  bool active = state_is_active(p->state);
  int32_t elapsed = printer_elapsed_now(p);
  if (active) {
    int32_t left = printer_time_left_now(p);
    if (left >= 0) {
      fmt_duration(t, sizeof(t), left);
      n = add_cell(cells, n, ICON_CLOCK, t);
    }
    time_t eta = printer_eta(p);
    if (eta > 0) {
      fmt_eta(t, sizeof(t), eta);
      n = add_cell(cells, n, ICON_FLAG, t);
    }
  } else if (elapsed > 0 && (p->state == PS_COMPLETE || p->state == PS_CANCELLED || p->state == PS_ERROR)) {
    fmt_duration(t, sizeof(t), elapsed);
    n = add_cell(cells, n, ICON_HOURGLASS, t);
  }
  if (p->nozzle != TEMP_NONE) {
    fmt_temp(t, sizeof(t), p->nozzle, p->nozzle_target, !compact);
    n = add_cell(cells, n, ICON_NOZZLE, t);
  }
  if (p->bed != TEMP_NONE) {
    fmt_temp(t, sizeof(t), p->bed, p->bed_target, !compact);
    n = add_cell(cells, n, ICON_BED, t);
  }
  if (p->layer >= 0) {
    if (p->layer_total > 0) {
      snprintf(t, sizeof(t), "%d/%d", p->layer, p->layer_total);
    } else {
      snprintf(t, sizeof(t), "%d", p->layer);
    }
    n = add_cell(cells, n, ICON_LAYERS, t);
  }
  if (active && elapsed >= 0) {
    fmt_duration(t, sizeof(t), elapsed);
    n = add_cell(cells, n, ICON_HOURGLASS, t);
  }
  return n;
}

static void draw_center(GContext *ctx, GRect ring, int t, const Printer *p, GColor accent, bool compact) {
  const Theme *th = theme();
  GPoint c = grect_center_point(&ring);

  if (state_is_active(p->state) && p->progress >= 0) {
    char num[6];
    snprintf(num, sizeof(num), "%d", p->progress / 10);
    GFont nf = fonts_get_system_font(compact ? FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM : FONT_KEY_LECO_32_BOLD_NUMBERS);
    GFont pf = fonts_get_system_font(compact ? FONT_KEY_GOTHIC_14_BOLD : FONT_KEY_GOTHIC_18_BOLD);
    int num_w = text_width(num, nf);
    int pct_w = text_width("%", pf);
    int num_h = compact ? 26 : 32;
    int x = c.x - (num_w + pct_w + 1) / 2;
    int y = c.y - num_h / 2 - (compact ? 8 : 10);
    graphics_context_set_text_color(ctx, th->text);
    graphics_draw_text(ctx, num, nf, GRect(x, y, num_w + 4, num_h + 8), GTextOverflowModeWordWrap,
                       GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(ctx, th->muted);
    int py = y + num_h - (compact ? 14 : 18);
    graphics_draw_text(ctx, "%", pf, GRect(x + num_w + 1, py, pct_w + 4, 24), GTextOverflowModeWordWrap,
                       GTextAlignmentLeft, NULL);
    if (p->flags & PF_ESTIMATED) {
      graphics_draw_text(ctx, "~", pf, GRect(x - 12, py, 12, 24), GTextOverflowModeWordWrap, GTextAlignmentLeft,
                         NULL);
    }
  } else if (p->state == PS_IDLE && p->nozzle != TEMP_NONE) {
    char temp[12];
    snprintf(temp, sizeof(temp), "%d°", p->nozzle);
    graphics_context_set_text_color(ctx, th->text);
    graphics_draw_text(ctx, temp, fonts_get_system_font(compact ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_28_BOLD),
                       GRect(ring.origin.x, c.y - (compact ? 22 : 27), ring.size.w, 34), GTextOverflowModeWordWrap,
                       GTextAlignmentCenter, NULL);
  } else {
    draw_state_glyph(ctx, p->state, GPoint(c.x, c.y - (compact ? 7 : 9)), compact ? 9 : 12, accent);
  }

  draw_text_fit(ctx, state_label(p->state), fonts_get_system_font(compact ? FONT_KEY_GOTHIC_14 : FONT_KEY_GOTHIC_14_BOLD),
                GRect(ring.origin.x + t, c.y + (compact ? 6 : 10), ring.size.w - 2 * t, 18), GTextAlignmentCenter,
                accent);
}

static void draw_page_dots(GContext *ctx, GRect b, GRect ring) {
  int n = model_count();
  if (n < 2) {
    return;
  }
  const Theme *th = theme();
  int spacing = 9;
  int x = b.size.w - 8;
  int y = ring.origin.y + ring.size.h / 2 - (n - 1) * spacing / 2;
  for (int i = 0; i < n; i++) {
    graphics_context_set_fill_color(ctx, i == s_index ? th->text : th->track);
    graphics_fill_circle(ctx, GPoint(x, y + i * spacing), i == s_index ? 3 : 2);
  }
}

static const char *kind_name(uint8_t kind) {
  switch (kind) {
    case KIND_KLIPPER: return "Klipper";
    case KIND_BAMBU: return "Bambu Lab";
    case KIND_PRUSA: return "Prusa";
    default: return "Printer";
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  const Theme *th = theme();
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, th->surface);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  Printer *p = model_get(s_index);
  if (!p) {
    ui_draw_header(ctx, GRect(0, 0, b.size.w, HEADER_HEIGHT), "PrintPulse", th->text, -1, th->surface, true);
    graphics_context_set_text_color(ctx, th->muted);
    graphics_draw_text(ctx, "This printer was removed.", fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(10, b.size.h / 2 - 20, b.size.w - 20, 40), GTextOverflowModeWordWrap,
                       GTextAlignmentCenter, NULL);
    return;
  }

  bool compact = b.size.h < 200;
  GColor accent = theme_state_color(p->state, false);
  // Narrow screens give the printer name the clock's space.
  ui_draw_header(ctx, GRect(0, 0, b.size.w, HEADER_HEIGHT), p->name, th->text, p->kind, th->surface,
                 b.size.w >= 180);

  int d = compact ? 76 : 104;
  int t = compact ? 7 : 10;
  GRect ring = GRect((b.size.w - d) / 2, HEADER_HEIGHT + (compact ? 2 : 4), d, d);
  int permille = 0;
  if (p->state == PS_COMPLETE) {
    permille = 1000;
  } else if (p->progress > 0 && (state_is_active(p->state) || p->state == PS_ERROR || p->state == PS_CANCELLED)) {
    permille = p->progress;
  }
  draw_ring(ctx, ring, t, permille, accent, th->track);
  draw_center(ctx, ring, t, p, accent, compact);
  draw_page_dots(ctx, b, ring);

  // File name (or the problem, when there is one and no file)
  bool problem = p->state == PS_ERROR || p->state == PS_OFFLINE || p->state == PS_ATTENTION;
  const char *primary = p->file;
  bool message_used = false;
  if (!primary[0]) {
    if (problem && p->message[0]) {
      primary = p->message;
      message_used = true;
    } else if (p->state == PS_IDLE) {
      primary = "Ready to print";
    } else {
      primary = kind_name(p->kind);
    }
  }
  int y = ring.origin.y + d + 2;
  draw_text_fit(ctx, primary, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GRect(8, y, b.size.w - 16, 22),
                GTextAlignmentCenter, th->text);
  y += 22;

  char secondary[MSG_LEN + 24] = "";
  if (p->flags & PF_STALE) {
    char seen[12];
    fmt_clock(seen, sizeof(seen), p->updated);
    snprintf(secondary, sizeof(secondary), "Can't reach printer · seen %s", seen);
  } else if (p->message[0] && !message_used) {
    snprintf(secondary, sizeof(secondary), "%s", p->message);
  }
  if (secondary[0]) {
    draw_text_fit(ctx, secondary, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(8, y - 2, b.size.w - 16, 18),
                  GTextAlignmentCenter, problem || (p->flags & PF_STALE) ? accent : th->muted);
    y += 16;
  }

  // Stats grid, two columns
  Cell cells[MAX_CELLS];
  int n = build_cells(p, cells, compact);
  int row_h = compact ? 20 : 21;
  int rows = (b.size.h - y - 2) / row_h;
  if (n > rows * 2) {
    n = rows * 2;
  }
  int col_w = (b.size.w - 16) / 2;
  GFont font = fonts_get_system_font(compact ? FONT_KEY_GOTHIC_14_BOLD : FONT_KEY_GOTHIC_18_BOLD);
  for (int i = 0; i < n; i++) {
    int cx = 8 + (i % 2) * col_w + 4;
    int cy = y + (i / 2) * row_h;
    draw_icon(ctx, cells[i].icon, GPoint(cx, cy + (row_h - ICON_SIZE) / 2), th->muted);
    draw_text_fit(ctx, cells[i].text, font,
                  GRect(cx + ICON_SIZE + 5, cy + (compact ? 0 : -3), col_w - ICON_SIZE - 8, 22),
                  GTextAlignmentLeft, th->text);
  }
}

static void up_click(ClickRecognizerRef recognizer, void *context) {
  app_note_user_activity();
  if (s_index > 0) {
    s_index--;
    layer_mark_dirty(s_canvas);
  }
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  app_note_user_activity();
  if (s_index < model_count() - 1) {
    s_index++;
    layer_mark_dirty(s_canvas);
  }
}

static void select_click(ClickRecognizerRef recognizer, void *context) {
  app_note_user_activity();
  comm_request_refresh();
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
  list_window_select(s_index);
}

void detail_window_push(int index) {
  s_index = index;
  if (!s_window) {
    s_window = window_create();
    window_set_click_config_provider(s_window, click_config);
    window_set_window_handlers(s_window, (WindowHandlers){
      .load = window_load,
      .unload = window_unload,
    });
  }
  if (window_stack_contains_window(s_window)) {
    detail_window_refresh();
    return;
  }
  window_stack_push(s_window, true);
}

void detail_window_refresh(void) {
  if (!s_canvas) {
    return;
  }
  int n = model_count();
  if (s_index >= n && n > 0) {
    s_index = n - 1;
  }
  layer_mark_dirty(s_canvas);
}

void detail_window_destroy(void) {
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}

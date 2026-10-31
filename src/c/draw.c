#include "draw.h"
#include "model.h"
#include "theme.h"

void draw_bar(GContext *ctx, GRect r, int permille, GColor fill, GColor track) {
  int radius = r.size.h / 2;
#if defined(PBL_COLOR)
  graphics_context_set_fill_color(ctx, track);
  graphics_fill_rect(ctx, r, radius, GCornersAll);
#else
  graphics_context_set_stroke_color(ctx, track);
  graphics_draw_round_rect(ctx, r, radius);
#endif
  if (permille <= 0) {
    return;
  }
  if (permille > 1000) {
    permille = 1000;
  }
  int w = r.size.w * permille / 1000;
  if (w < r.size.h) {
    w = r.size.h;
  }
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, GRect(r.origin.x, r.origin.y, w, r.size.h), radius, GCornersAll);
}

void draw_ring(GContext *ctx, GRect r, int t, int permille, GColor fill, GColor track) {
  GRect mid = grect_inset(r, GEdgeInsets(t / 2));
#if defined(PBL_COLOR)
  graphics_context_set_fill_color(ctx, track);
  graphics_fill_radial(ctx, r, GOvalScaleModeFitCircle, t, 0, TRIG_MAX_ANGLE);
#else
  graphics_context_set_stroke_color(ctx, track);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, grect_center_point(&r), mid.size.w / 2);
#endif
  if (permille <= 0) {
    return;
  }
  if (permille > 1000) {
    permille = 1000;
  }
  int32_t end = TRIG_MAX_ANGLE * permille / 1000;
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_radial(ctx, r, GOvalScaleModeFitCircle, t, 0, end);
  if (permille < 1000) {
    // Rounded ends make the arc read as a modern progress ring.
    int cap = (t - 1) / 2;
    graphics_fill_circle(ctx, gpoint_from_polar(mid, GOvalScaleModeFitCircle, 0), cap);
    graphics_fill_circle(ctx, gpoint_from_polar(mid, GOvalScaleModeFitCircle, end), cap);
  }
}

void draw_chip(GContext *ctx, GRect r, uint8_t kind, bool hl) {
#if defined(PBL_COLOR)
  GColor fill = theme_kind_color(kind);
  GColor letter = GColorWhite;
#else
  const Theme *th = theme();
  GColor fill = hl ? th->text_hl : th->text;
  GColor letter = hl ? th->card_hl : th->card;
#endif
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, r, r.size.h / 4 + 1, GCornersAll);

  char text[2] = { theme_kind_letter(kind), '\0' };
  bool big = r.size.h >= 20;
  GFont font = fonts_get_system_font(big ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14_BOLD);
  GRect box = GRect(r.origin.x, r.origin.y + r.size.h / 2 - (big ? 12 : 9), r.size.w, r.size.h);
  graphics_context_set_text_color(ctx, letter);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

void draw_icon(GContext *ctx, IconId icon, GPoint o, GColor c) {
  graphics_context_set_stroke_color(ctx, c);
  graphics_context_set_fill_color(ctx, c);
  graphics_context_set_stroke_width(ctx, 1);
  switch (icon) {
    case ICON_CLOCK:
      graphics_draw_circle(ctx, GPoint(o.x + 6, o.y + 6), 5);
      graphics_draw_line(ctx, GPoint(o.x + 6, o.y + 6), GPoint(o.x + 6, o.y + 3));
      graphics_draw_line(ctx, GPoint(o.x + 6, o.y + 6), GPoint(o.x + 8, o.y + 7));
      break;
    case ICON_FLAG:
      graphics_draw_line(ctx, GPoint(o.x + 2, o.y + 1), GPoint(o.x + 2, o.y + 11));
      graphics_fill_rect(ctx, GRect(o.x + 3, o.y + 1, 4, 3), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(o.x + 7, o.y + 4, 3, 3), 0, GCornerNone);
      graphics_draw_rect(ctx, GRect(o.x + 3, o.y + 1, 7, 6));
      break;
    case ICON_NOZZLE:
      // heatsink fins, heater block, nozzle tip
      graphics_draw_line(ctx, GPoint(o.x + 3, o.y), GPoint(o.x + 8, o.y));
      graphics_draw_line(ctx, GPoint(o.x + 3, o.y + 2), GPoint(o.x + 8, o.y + 2));
      graphics_fill_rect(ctx, GRect(o.x + 1, o.y + 4, 10, 4), 1, GCornersAll);
      graphics_fill_rect(ctx, GRect(o.x + 4, o.y + 8, 4, 1), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(o.x + 5, o.y + 9, 2, 2), 0, GCornerNone);
      break;
    case ICON_BED:
      graphics_fill_rect(ctx, GRect(o.x + 1, o.y + 9, 10, 2), 0, GCornerNone);
      for (int i = 0; i < 3; i++) {
        int x = o.x + 3 + i * 3;
        graphics_draw_line(ctx, GPoint(x, o.y + 1), GPoint(x - 1, o.y + 3));
        graphics_draw_line(ctx, GPoint(x - 1, o.y + 3), GPoint(x, o.y + 5));
        graphics_draw_line(ctx, GPoint(x, o.y + 5), GPoint(x - 1, o.y + 7));
      }
      break;
    case ICON_LAYERS:
      graphics_fill_rect(ctx, GRect(o.x + 3, o.y + 1, 6, 2), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(o.x + 2, o.y + 5, 8, 2), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(o.x + 1, o.y + 9, 10, 2), 0, GCornerNone);
      break;
    case ICON_HOURGLASS:
      graphics_draw_line(ctx, GPoint(o.x + 2, o.y + 1), GPoint(o.x + 9, o.y + 1));
      graphics_draw_line(ctx, GPoint(o.x + 2, o.y + 10), GPoint(o.x + 9, o.y + 10));
      graphics_draw_line(ctx, GPoint(o.x + 3, o.y + 2), GPoint(o.x + 8, o.y + 9));
      graphics_draw_line(ctx, GPoint(o.x + 8, o.y + 2), GPoint(o.x + 3, o.y + 9));
      graphics_fill_rect(ctx, GRect(o.x + 4, o.y + 8, 4, 2), 0, GCornerNone);
      break;
    case ICON_PHONE_OFF:
      graphics_draw_round_rect(ctx, GRect(o.x + 3, o.y, 7, 12), 1);
      graphics_draw_line(ctx, GPoint(o.x + 1, o.y + 11), GPoint(o.x + 11, o.y + 1));
      break;
    case ICON_REFRESH: {
      GRect r = GRect(o.x + 1, o.y + 1, 10, 10);
      graphics_draw_arc(ctx, r, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(60), DEG_TO_TRIGANGLE(360));
      graphics_fill_rect(ctx, GRect(o.x + 7, o.y, 4, 2), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(o.x + 9, o.y, 2, 4), 0, GCornerNone);
      break;
    }
  }
}

void draw_state_glyph(GContext *ctx, uint8_t state, GPoint c, int s, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_fill_color(ctx, color);
  int w = s / 3;
  if (w < 3) w = 3;
  graphics_context_set_stroke_width(ctx, w | 1);
  switch (state) {
    case PS_COMPLETE:
      graphics_draw_line(ctx, GPoint(c.x - s, c.y), GPoint(c.x - s / 3, c.y + s * 2 / 3));
      graphics_draw_line(ctx, GPoint(c.x - s / 3, c.y + s * 2 / 3), GPoint(c.x + s, c.y - s * 2 / 3));
      break;
    case PS_ERROR:
    case PS_ATTENTION:
      graphics_draw_line(ctx, GPoint(c.x, c.y - s), GPoint(c.x, c.y + s / 4));
      graphics_fill_circle(ctx, GPoint(c.x, c.y + s - w / 2), w / 2 + 1);
      break;
    case PS_CANCELLED:
      graphics_draw_line(ctx, GPoint(c.x - s * 2 / 3, c.y - s * 2 / 3), GPoint(c.x + s * 2 / 3, c.y + s * 2 / 3));
      graphics_draw_line(ctx, GPoint(c.x + s * 2 / 3, c.y - s * 2 / 3), GPoint(c.x - s * 2 / 3, c.y + s * 2 / 3));
      break;
    case PS_PAUSED:
      graphics_fill_rect(ctx, GRect(c.x - s * 2 / 3, c.y - s, w + 1, s * 2), 2, GCornersAll);
      graphics_fill_rect(ctx, GRect(c.x + s * 2 / 3 - w - 1, c.y - s, w + 1, s * 2), 2, GCornersAll);
      break;
    case PS_OFFLINE:
      graphics_context_set_stroke_width(ctx, 3);
      graphics_draw_circle(ctx, c, s);
      graphics_draw_line(ctx, GPoint(c.x - s * 2 / 3, c.y + s * 2 / 3), GPoint(c.x + s * 2 / 3, c.y - s * 2 / 3));
      break;
    default:
      for (int i = -1; i <= 1; i++) {
        graphics_fill_circle(ctx, GPoint(c.x + i * (s * 2 / 3), c.y), w / 2 + 1);
      }
      break;
  }
  graphics_context_set_stroke_width(ctx, 1);
}

void draw_printer_art(GContext *ctx, GPoint c, GColor frame, GColor accent) {
  // A tiny printer: frame, gantry with hotend, and a part being printed.
  graphics_context_set_stroke_color(ctx, frame);
  graphics_context_set_fill_color(ctx, frame);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_round_rect(ctx, GRect(c.x - 30, c.y - 26, 60, 52), 6);
  graphics_draw_line(ctx, GPoint(c.x - 28, c.y - 12), GPoint(c.x + 28, c.y - 12));
  graphics_context_set_stroke_width(ctx, 1);
  graphics_fill_rect(ctx, GRect(c.x - 6, c.y - 17, 12, 9), 2, GCornersAll);
  graphics_fill_rect(ctx, GRect(c.x - 2, c.y - 8, 4, 4), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(c.x - 22, c.y + 17, 44, 3), 1, GCornersAll);
  graphics_context_set_fill_color(ctx, accent);
  graphics_fill_rect(ctx, GRect(c.x - 14, c.y + 12, 28, 4), 1, GCornersAll);
  graphics_fill_rect(ctx, GRect(c.x - 12, c.y + 7, 24, 4), 1, GCornersAll);
  graphics_fill_rect(ctx, GRect(c.x - 9, c.y + 2, 18, 4), 1, GCornersAll);
}

int text_width(const char *text, GFont font) {
  return graphics_text_layout_get_content_size(text, font, GRect(0, 0, 1000, 60), GTextOverflowModeWordWrap,
                                               GTextAlignmentLeft).w;
}

// Copies `text` into `out`, shortened with "…" until it fits `max_w`.
static int fit_text(char *out, size_t size, const char *text, GFont font, int max_w) {
  strncpy(out, text, size - 1);
  out[size - 1] = '\0';
  int w = text_width(out, font);
  if (w <= max_w) {
    return w;
  }
  size_t len = strlen(out);
  while (len > 0) {
    do {
      len--;  // step back a whole UTF-8 character
    } while (len > 0 && ((uint8_t)out[len] & 0xC0) == 0x80);
    size_t cut = len;
    while (cut > 0 && out[cut - 1] == ' ') {
      cut--;
    }
    if (cut + 4 > size) {
      continue;
    }
    memcpy(out + cut, "\xe2\x80\xa6", 4);
    w = text_width(out, font);
    if (w <= max_w) {
      return w;
    }
  }
  out[0] = '\0';
  return 0;
}

void draw_text_fit(GContext *ctx, const char *text, GFont font, GRect box, GTextAlignment align, GColor color) {
  char buf[64];
  fit_text(buf, sizeof(buf), text, font, box.size.w);
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, buf, font, box, GTextOverflowModeWordWrap, align, NULL);
}

int draw_text_run(GContext *ctx, const char *text, GFont font, GColor color, int x, int y, int max_w) {
  if (max_w <= 0 || !text[0]) {
    return x;
  }
  char buf[64];
  int w = fit_text(buf, sizeof(buf), text, font, max_w);
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, buf, font, GRect(x, y, max_w, 24), GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  return x + w;
}

void fmt_duration(char *buf, size_t size, int32_t seconds) {
  if (seconds < 0) {
    snprintf(buf, size, "--");
    return;
  }
  int32_t m = (seconds + 30) / 60;
  if (m < 1) {
    snprintf(buf, size, "<1m");
  } else if (m < 60) {
    snprintf(buf, size, "%dm", (int)m);
  } else if (m < 24 * 60) {
    snprintf(buf, size, "%dh %02dm", (int)(m / 60), (int)(m % 60));
  } else {
    snprintf(buf, size, "%dd %dh", (int)(m / 1440), (int)((m % 1440) / 60));
  }
}

void fmt_clock(char *buf, size_t size, time_t when) {
  struct tm *t = localtime(&when);
  if (clock_is_24h_style()) {
    strftime(buf, size, "%H:%M", t);
  } else {
    int h = t->tm_hour % 12;
    snprintf(buf, size, "%d:%02d%s", h ? h : 12, t->tm_min, t->tm_hour < 12 ? "am" : "pm");
  }
}

void fmt_since(char *buf, size_t size, time_t when) {
  int32_t ago = (int32_t)(time(NULL) - when);
  if (ago < 60) {
    snprintf(buf, size, "just now");
  } else if (ago < 3600) {
    snprintf(buf, size, "%dm ago", (int)(ago / 60));
  } else {
    fmt_clock(buf, size, when);
  }
}

void fmt_temp(char *buf, size_t size, int16_t current, int16_t target, bool with_target) {
  if (current == TEMP_NONE) {
    snprintf(buf, size, "--");
  } else if (with_target && target > 0) {
    snprintf(buf, size, "%d°/%d°", current, target);
  } else {
    snprintf(buf, size, "%d°", current);
  }
}

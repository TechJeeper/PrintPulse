#include "theme.h"
#include "model.h"

static Theme s_theme;

void theme_apply(uint8_t which) {
  bool light = which == 1;
#if defined(PBL_COLOR)
  if (light) {
    s_theme = (Theme){
      .bg = GColorLightGray, .surface = GColorWhite,
      .card = GColorWhite, .card_hl = GColorWhite,
      .border = GColorClear, .focus = GColorBlack,
      .text = GColorBlack, .text_hl = GColorBlack,
      .muted = GColorDarkGray, .muted_hl = GColorDarkGray,
      .track = GColorLightGray, .track_hl = GColorLightGray,
      .toast = GColorBlack, .toast_text = GColorWhite,
      .light = true,
    };
  } else {
    s_theme = (Theme){
      .bg = GColorBlack, .surface = GColorBlack,
      .card = GColorOxfordBlue, .card_hl = GColorOxfordBlue,
      .border = GColorClear, .focus = GColorWhite,
      .text = GColorWhite, .text_hl = GColorWhite,
      .muted = GColorLightGray, .muted_hl = GColorLightGray,
      .track = GColorDarkGray, .track_hl = GColorDarkGray,
      .toast = GColorWhite, .toast_text = GColorBlack,
      .light = false,
    };
  }
#else
  GColor fg = light ? GColorBlack : GColorWhite;
  GColor bg = light ? GColorWhite : GColorBlack;
  s_theme = (Theme){
    .bg = bg, .surface = bg,
    .card = bg, .card_hl = fg,
    .border = fg, .focus = fg,
    .text = fg, .text_hl = bg,
    .muted = fg, .muted_hl = bg,
    .track = fg, .track_hl = bg,
    .toast = fg, .toast_text = bg,
    .light = light,
  };
#endif
}

const Theme *theme(void) {
  return &s_theme;
}

GColor theme_state_color(uint8_t state, bool highlighted) {
#if defined(PBL_COLOR)
  bool light = s_theme.light;
  switch (state) {
    case PS_PRINTING: return light ? GColorCobaltBlue : GColorVividCerulean;
    case PS_PREPARING: return light ? GColorBlueMoon : GColorPictonBlue;
    case PS_PAUSED: return light ? GColorWindsorTan : GColorChromeYellow;
    case PS_ATTENTION: return GColorOrange;
    case PS_COMPLETE: return light ? GColorIslamicGreen : GColorMalachite;
    case PS_CANCELLED: return light ? GColorWindsorTan : GColorRajah;
    case PS_ERROR: return light ? GColorDarkCandyAppleRed : GColorFolly;
    default: return highlighted ? s_theme.muted_hl : s_theme.muted;
  }
#else
  return highlighted ? s_theme.text_hl : s_theme.text;
#endif
}

GColor theme_kind_color(uint8_t kind) {
  switch (kind) {
    case KIND_KLIPPER: return PBL_IF_COLOR_ELSE(GColorSunsetOrange, GColorWhite);
    case KIND_BAMBU: return PBL_IF_COLOR_ELSE(GColorJaegerGreen, GColorWhite);
    case KIND_PRUSA: return PBL_IF_COLOR_ELSE(GColorOrange, GColorWhite);
    default: return PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite);
  }
}

char theme_kind_letter(uint8_t kind) {
  switch (kind) {
    case KIND_KLIPPER: return 'K';
    case KIND_BAMBU: return 'B';
    case KIND_PRUSA: return 'P';
    default: return '?';
  }
}

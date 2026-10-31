#include <pebble.h>
#include "app.h"
#include "comm.h"
#include "model.h"
#include "theme.h"
#include "ui.h"

// Consecutive wakeup launches that found nothing new. Stops us from waking the
// watch over and over when a printer's estimate keeps slipping.
#define KEY_WAKE_STREAK 5
#define WAKE_STREAK_MAX 3

#define SYNC_INDICATOR_MS 30000
#define WAKE_TIMEOUT_MS 45000
#define WAKE_EXIT_DELAY_MS 3000

static bool s_syncing;
static bool s_synced;
static bool s_connected;
static bool s_wake_check;   // launched by our wakeup and the user hasn't taken over
static time_t s_last_sync;
static AppTimer *s_sync_timer;
static AppTimer *s_exit_timer;

bool app_is_syncing(void) { return s_syncing; }
bool app_is_connected(void) { return s_connected; }
bool app_has_synced(void) { return s_synced; }
time_t app_last_sync(void) { return s_last_sync; }

// ---- wakeups -------------------------------------------------------------

static void schedule_wakeup(void) {
  wakeup_cancel_all();
  if (!model_settings()->wake_on_done || persist_read_int(KEY_WAKE_STREAK) >= WAKE_STREAK_MAX) {
    return;
  }
  time_t soonest = 0;
  for (int i = 0; i < model_count(); i++) {
    time_t eta = printer_eta(model_get(i));
    if (eta > 0 && (soonest == 0 || eta < soonest)) {
      soonest = eta;
    }
  }
  if (!soonest) {
    return;
  }
  time_t when = soonest + 60;
  time_t earliest = time(NULL) + 180;
  if (when < earliest) {
    when = earliest;
  }
  // Another app may own the same minute; nudge forward until a slot is free.
  for (int i = 0; i < 6; i++) {
    WakeupId id = wakeup_schedule(when + i * 61, 0, false);
    if (id >= 0 || id != E_RANGE) {
      return;
    }
  }
}

static void quit(void *context) {
  s_exit_timer = NULL;
  window_stack_pop_all(false);
}

static void wake_give_up(void *context) {
  s_exit_timer = NULL;
  s_wake_check = false;
  persist_write_int(KEY_WAKE_STREAK, persist_read_int(KEY_WAKE_STREAK) + 1);
  window_stack_pop_all(false);
}

void app_note_user_activity(void) {
  if (!s_wake_check && !s_exit_timer) {
    return;
  }
  s_wake_check = false;
  if (s_exit_timer) {
    app_timer_cancel(s_exit_timer);
    s_exit_timer = NULL;
  }
  persist_write_int(KEY_WAKE_STREAK, 0);
}

// ---- sync events ---------------------------------------------------------

static void sync_indicator_timeout(void *context) {
  s_sync_timer = NULL;
  s_syncing = false;
  ui_refresh();
}

void app_on_sync_begin(void) {
  s_syncing = true;
  if (s_sync_timer) {
    app_timer_reschedule(s_sync_timer, SYNC_INDICATOR_MS);
  } else {
    s_sync_timer = app_timer_register(SYNC_INDICATOR_MS, sync_indicator_timeout, NULL);
  }
  ui_refresh();
}

static const char *event_verb(uint8_t type) {
  switch (type) {
    case EV_FINISHED: return "finished";
    case EV_FAILED: return "failed";
    case EV_CANCELLED: return "cancelled";
    case EV_PAUSED: return "paused";
    case EV_ATTENTION: return "needs you";
    default: return "changed";
  }
}

static void vibrate_for(uint8_t type) {
  static const uint32_t s_done[] = { 150, 100, 150, 100, 400 };
  static const uint32_t s_alarm[] = { 500, 200, 500, 200, 500 };
  switch (type) {
    case EV_FINISHED:
      vibes_enqueue_custom_pattern((VibePattern){ .durations = s_done, .num_segments = ARRAY_LENGTH(s_done) });
      break;
    case EV_FAILED:
    case EV_ATTENTION:
      vibes_enqueue_custom_pattern((VibePattern){ .durations = s_alarm, .num_segments = ARRAY_LENGTH(s_alarm) });
      break;
    default:
      vibes_short_pulse();
      break;
  }
}

static void announce(const PrinterEvent *events, int n) {
  int best = 0;
  for (int i = 1; i < n; i++) {
    if (events[i].type > events[best].type) {
      best = i;
    }
  }
  int index = model_index_of(events[best].uid);
  if (index < 0) {
    return;
  }
  const Printer *p = model_get(index);
  char text[48];
  if (n > 1) {
    snprintf(text, sizeof(text), "%s %s +%d", p->name, event_verb(events[best].type), n - 1);
  } else {
    snprintf(text, sizeof(text), "%s %s", p->name, event_verb(events[best].type));
  }
  if (model_settings()->vibe_on_done) {
    vibrate_for(events[best].type);
  }
  if (s_wake_check) {
    detail_window_push(index);
  }
  ui_toast(text, theme_state_color(p->state, false));
}

void app_on_sync_done(void) {
  s_syncing = false;
  s_synced = true;
  s_last_sync = time(NULL);
  if (s_sync_timer) {
    app_timer_cancel(s_sync_timer);
    s_sync_timer = NULL;
  }

  PrinterEvent events[MAX_PRINTERS];
  int n = model_take_events(events, MAX_PRINTERS);
  model_save();
  ui_refresh();
  if (n > 0) {
    announce(events, n);
  }

  if (s_wake_check) {
    s_wake_check = false;
    if (s_exit_timer) {
      app_timer_cancel(s_exit_timer);
      s_exit_timer = NULL;
    }
    persist_write_int(KEY_WAKE_STREAK, n > 0 ? 0 : persist_read_int(KEY_WAKE_STREAK) + 1);
    if (n == 0) {
      // Nothing finished yet: slip quietly back to whatever the user was doing.
      s_exit_timer = app_timer_register(WAKE_EXIT_DELAY_MS, quit, NULL);
    }
  }
  schedule_wakeup();
}

void app_on_settings_changed(void) {
  ui_apply_theme();
  schedule_wakeup();
}

// ---- launcher glance -----------------------------------------------------

#define COUNTDOWN "{time_until(%ld)|format(>=1H:'%%aR left',>0S:'%%aM left','finishing')}"

static void glance_reload(AppGlanceReloadSession *session, size_t limit, void *context) {
  if (limit < 1) {
    return;
  }
  int n = model_count(), active = 0;
  const Printer *soonest = NULL, *problem = NULL, *done = NULL;
  time_t soonest_eta = 0;
  for (int i = 0; i < n; i++) {
    const Printer *p = model_get(i);
    if ((p->state == PS_ERROR || p->state == PS_ATTENTION) && !problem) problem = p;
    if (p->state == PS_COMPLETE && !done) done = p;
    if (p->state == PS_PRINTING || p->state == PS_PREPARING) active++;
    time_t eta = printer_eta(p);
    if (eta > 0 && (!soonest_eta || eta < soonest_eta)) {
      soonest_eta = eta;
      soonest = p;
    }
  }

  char text[128];
  time_t expires = APP_GLANCE_SLICE_NO_EXPIRATION;
  if (problem) {
    snprintf(text, sizeof(text), "%s needs attention", problem->name);
  } else if (soonest) {
    if (active > 1) {
      snprintf(text, sizeof(text), "%d printing · " COUNTDOWN, active, (long)soonest_eta);
    } else {
      snprintf(text, sizeof(text), "%s · " COUNTDOWN, soonest->name, (long)soonest_eta);
    }
    expires = soonest_eta + 10 * 60;
  } else if (active) {
    snprintf(text, sizeof(text), active == 1 ? "1 print running" : "%d prints running", active);
  } else if (done) {
    snprintf(text, sizeof(text), "%s finished", done->name);
  } else if (n > 0) {
    snprintf(text, sizeof(text), n == 1 ? "1 printer idle" : "%d printers idle", n);
  } else {
    snprintf(text, sizeof(text), "Add printers in the Pebble app");
  }

  app_glance_add_slice(session, (AppGlanceSlice){
    .layout = { .icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = text },
    .expiration_time = expires,
  });
  if (soonest && limit > 1) {
    snprintf(text, sizeof(text), "%s should be done", soonest->name);
    app_glance_add_slice(session, (AppGlanceSlice){
      .layout = { .icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = text },
      .expiration_time = APP_GLANCE_SLICE_NO_EXPIRATION,
    });
  }
}

// ---- lifecycle -----------------------------------------------------------

static void on_connection(bool connected) {
  s_connected = connected;
  ui_refresh();
}

static void on_tick(struct tm *tick_time, TimeUnits changed) {
  ui_refresh();
}

static void on_wakeup(WakeupId id, int32_t cookie) {
  comm_request_refresh();
}

static void init(void) {
  model_load();
  ui_init();
  s_connected = connection_service_peek_pebble_app_connection();
  s_wake_check = launch_reason() == APP_LAUNCH_WAKEUP;
  if (s_wake_check) {
    s_exit_timer = app_timer_register(s_connected ? WAKE_TIMEOUT_MS : 4000, wake_give_up, NULL);
  } else {
    persist_write_int(KEY_WAKE_STREAK, 0);
  }

  list_window_push();
  comm_init();
  connection_service_subscribe((ConnectionHandlers){ .pebble_app_connection_handler = on_connection });
  tick_timer_service_subscribe(MINUTE_UNIT, on_tick);
  wakeup_service_subscribe(on_wakeup);

  if (s_wake_check) {
    ui_toast("Checking on your prints…", theme_state_color(PS_PRINTING, false));
  }
}

static void deinit(void) {
  model_save();
  schedule_wakeup();
  app_glance_reload(glance_reload, NULL);
  tick_timer_service_unsubscribe();
  connection_service_unsubscribe();
  ui_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

#include "model.h"

#define DATA_VERSION 1

#define KEY_VERSION 1
#define KEY_COUNT 2
#define KEY_SETTINGS 3
#define KEY_PRINTER_BASE 100

static Printer s_printers[MAX_PRINTERS];
static int s_count;
static PrinterEvent s_events[MAX_PRINTERS];
static int s_event_count;
static Settings s_settings = {
  .theme = 0,
  .vibe_on_done = 1,
  .wake_on_done = 1,
  .interval = 30,
};

void model_load(void) {
  if (persist_exists(KEY_SETTINGS)) {
    persist_read_data(KEY_SETTINGS, &s_settings, sizeof(s_settings));
  }
  if (persist_read_int(KEY_VERSION) != DATA_VERSION) {
    s_count = 0;
    return;
  }
  s_count = persist_read_int(KEY_COUNT);
  if (s_count < 0 || s_count > MAX_PRINTERS) {
    s_count = 0;
  }
  for (int i = 0; i < s_count; i++) {
    if (persist_read_data(KEY_PRINTER_BASE + i, &s_printers[i], sizeof(Printer)) != (int)sizeof(Printer)) {
      s_count = i;
      break;
    }
  }
}

void model_save(void) {
  persist_write_int(KEY_VERSION, DATA_VERSION);
  persist_write_int(KEY_COUNT, s_count);
  for (int i = 0; i < s_count; i++) {
    persist_write_data(KEY_PRINTER_BASE + i, &s_printers[i], sizeof(Printer));
  }
  for (int i = s_count; i < MAX_PRINTERS; i++) {
    persist_delete(KEY_PRINTER_BASE + i);
  }
}

int model_count(void) {
  return s_count;
}

Printer *model_get(int index) {
  return (index >= 0 && index < s_count) ? &s_printers[index] : NULL;
}

int model_index_of(uint32_t uid) {
  for (int i = 0; i < s_count; i++) {
    if (s_printers[i].uid == uid) {
      return i;
    }
  }
  return -1;
}

static PrinterEventType classify(const Printer *old, const Printer *now) {
  if (now->flags & PF_STALE || old->state == now->state) {
    return EV_NONE;
  }
  if (old->was_active) {
    switch (now->state) {
      case PS_COMPLETE: return EV_FINISHED;
      case PS_ERROR: return EV_FAILED;
      case PS_CANCELLED: return EV_CANCELLED;
      default: break;
    }
  }
  if (old->state == PS_PRINTING || old->state == PS_PREPARING) {
    if (now->state == PS_ATTENTION) return EV_ATTENTION;
    if (now->state == PS_PAUSED) return EV_PAUSED;
  }
  return EV_NONE;
}

void model_put(int index, const Printer *printer) {
  if (index < 0 || index >= MAX_PRINTERS) {
    return;
  }
  Printer next = *printer;
  int old_index = model_index_of(next.uid);
  bool was_active = false;
  if (old_index >= 0) {
    const Printer *old = &s_printers[old_index];
    was_active = old->was_active;
    PrinterEventType ev = classify(old, &next);
    if (ev != EV_NONE && s_event_count < MAX_PRINTERS) {
      s_events[s_event_count++] = (PrinterEvent){ .uid = next.uid, .type = ev };
    }
  }
  // Losing contact (or stale data) keeps the memory of a running print, so a
  // finish that happens while the phone is away from home is still announced.
  if (state_is_active(next.state)) {
    next.was_active = 1;
  } else if (next.state == PS_OFFLINE || next.state == PS_UNKNOWN || (next.flags & PF_STALE)) {
    next.was_active = was_active;
  } else {
    next.was_active = 0;
  }
  s_printers[index] = next;
  if (index >= s_count) {
    s_count = index + 1;
  }
}

void model_set_count(int count) {
  if (count < 0) count = 0;
  if (count > MAX_PRINTERS) count = MAX_PRINTERS;
  s_count = count;
}

int model_take_events(PrinterEvent *out, int max) {
  int n = s_event_count < max ? s_event_count : max;
  memcpy(out, s_events, n * sizeof(PrinterEvent));
  s_event_count = 0;
  return n;
}

Settings *model_settings(void) {
  return &s_settings;
}

void model_save_settings(void) {
  persist_write_data(KEY_SETTINGS, &s_settings, sizeof(s_settings));
}

bool state_is_active(uint8_t state) {
  return state == PS_PREPARING || state == PS_PRINTING || state == PS_PAUSED || state == PS_ATTENTION;
}

bool printer_shows_progress(const Printer *p) {
  return state_is_active(p->state) || p->state == PS_COMPLETE;
}

const char *state_label(uint8_t state) {
  switch (state) {
    case PS_OFFLINE: return "Offline";
    case PS_IDLE: return "Ready";
    case PS_PREPARING: return "Preparing";
    case PS_PRINTING: return "Printing";
    case PS_PAUSED: return "Paused";
    case PS_ATTENTION: return "Attention";
    case PS_COMPLETE: return "Complete";
    case PS_CANCELLED: return "Cancelled";
    case PS_ERROR: return "Error";
    default: return "Connecting";
  }
}

int32_t printer_time_left_now(const Printer *p) {
  if (p->time_left < 0) {
    return -1;
  }
  if (p->state != PS_PRINTING) {
    return p->time_left;
  }
  int32_t passed = (int32_t)time(NULL) - p->updated;
  int32_t left = p->time_left - (passed > 0 ? passed : 0);
  return left > 0 ? left : 0;
}

int32_t printer_elapsed_now(const Printer *p) {
  if (p->elapsed < 0) {
    return -1;
  }
  if (p->state != PS_PRINTING) {
    return p->elapsed;
  }
  int32_t passed = (int32_t)time(NULL) - p->updated;
  return p->elapsed + (passed > 0 ? passed : 0);
}

time_t printer_eta(const Printer *p) {
  if (p->state != PS_PRINTING || p->time_left < 0) {
    return 0;
  }
  return (time_t)(p->updated + p->time_left);
}

#pragma once
#include <pebble.h>

// Printer records arrive from the phone (src/pkjs) and are cached in persistent
// storage so the app shows the last known state instantly on launch.

#define MAX_PRINTERS 8
#define NAME_LEN 24
#define FILE_LEN 48
#define MSG_LEN 48
#define TEMP_NONE (-999)

// Keep in sync with KIND in src/pkjs/model.js
typedef enum {
  KIND_KLIPPER = 0,
  KIND_BAMBU,
  KIND_PRUSA,
} PrinterKind;

// Keep in sync with STATE in src/pkjs/model.js
typedef enum {
  PS_UNKNOWN = 0,
  PS_OFFLINE,
  PS_IDLE,
  PS_PREPARING,
  PS_PRINTING,
  PS_PAUSED,
  PS_ATTENTION,
  PS_COMPLETE,
  PS_CANCELLED,
  PS_ERROR,
} PrinterState;

enum {
  PF_ESTIMATED = 1 << 0,  // progress/time are estimates (Bambu Cloud)
  PF_STALE = 1 << 1,      // printer unreachable; values are from `updated`
};

typedef struct {
  uint32_t uid;
  char name[NAME_LEN];
  char file[FILE_LEN];
  char message[MSG_LEN];
  uint8_t kind;
  uint8_t state;
  uint8_t flags;
  uint8_t was_active;     // a print was running last time we had fresh data
  int16_t progress;       // permille, -1 = unknown
  int16_t nozzle;         // degrees C, TEMP_NONE = unknown
  int16_t nozzle_target;
  int16_t bed;
  int16_t bed_target;
  int16_t layer;          // -1 = unknown
  int16_t layer_total;
  int32_t time_left;      // seconds as of `updated`, -1 = unknown
  int32_t elapsed;        // seconds as of `updated`, -1 = unknown
  int32_t updated;        // epoch seconds of the reading
} Printer;

typedef enum {
  EV_NONE = 0,
  EV_CANCELLED,
  EV_PAUSED,
  EV_FINISHED,
  EV_ATTENTION,
  EV_FAILED,              // ordered by importance
} PrinterEventType;

typedef struct {
  uint32_t uid;
  uint8_t type;
} PrinterEvent;

typedef struct {
  uint8_t theme;          // 0 = dark, 1 = light
  uint8_t vibe_on_done;
  uint8_t wake_on_done;
  uint8_t reserved;
  int32_t interval;       // phone poll interval, seconds
} Settings;

void model_load(void);
void model_save(void);

int model_count(void);
Printer *model_get(int index);
int model_index_of(uint32_t uid);
void model_put(int index, const Printer *printer);
void model_set_count(int count);
int model_take_events(PrinterEvent *out, int max);

Settings *model_settings(void);
void model_save_settings(void);

bool state_is_active(uint8_t state);
bool printer_shows_progress(const Printer *p);
const char *state_label(uint8_t state);
int32_t printer_time_left_now(const Printer *p);
int32_t printer_elapsed_now(const Printer *p);
time_t printer_eta(const Printer *p);

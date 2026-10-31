#pragma once
#include <pebble.h>

// App-level state shared by the UI and the phone link.

void app_on_sync_begin(void);
void app_on_sync_done(void);
void app_on_settings_changed(void);
void app_note_user_activity(void);

bool app_is_syncing(void);
bool app_is_connected(void);
bool app_has_synced(void);
time_t app_last_sync(void);

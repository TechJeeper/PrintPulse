#pragma once
#include <pebble.h>

// AppMessage protocol, keep in sync with CMD in src/pkjs/model.js
enum {
  CMD_SYNC_BEGIN = 1,   // phone -> watch: a poll started
  CMD_PRINTER = 2,      // phone -> watch: one printer record
  CMD_SYNC_DONE = 3,    // phone -> watch: poll finished, carries Count
  CMD_SETTINGS = 4,     // phone -> watch: user settings
  CMD_REFRESH = 5,      // watch -> phone: poll now
};

void comm_init(void);
void comm_request_refresh(void);

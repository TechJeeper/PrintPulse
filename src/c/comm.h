#pragma once
#include <pebble.h>

// AppMessage protocol, keep in sync with CMD/ACTION in src/pkjs/model.js
enum {
  CMD_SYNC_BEGIN = 1,   // phone -> watch: a poll started
  CMD_PRINTER = 2,      // phone -> watch: one printer record
  CMD_SYNC_DONE = 3,    // phone -> watch: poll finished, carries Count
  CMD_SETTINGS = 4,     // phone -> watch: user settings
  CMD_REFRESH = 5,      // watch -> phone: poll now
  CMD_NOTICE = 6,       // phone -> watch: short toast (Message)
  CMD_ACTION = 7,       // watch -> phone: pause/resume/stop (Index, Action)
};

enum {
  ACTION_PAUSE = 1,
  ACTION_RESUME = 2,
  ACTION_STOP = 3,
};

void comm_init(void);
void comm_request_refresh(void);
void comm_send_action(int index, int action);

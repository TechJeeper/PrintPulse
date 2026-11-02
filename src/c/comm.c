#include "comm.h"
#include "app.h"
#include "model.h"
#include "theme.h"
#include "ui.h"

static int32_t tuple_int(const Tuple *t) {
  if (t->type == TUPLE_INT) {
    switch (t->length) {
      case 1: return t->value->int8;
      case 2: return t->value->int16;
      default: return t->value->int32;
    }
  }
  if (t->type == TUPLE_UINT) {
    switch (t->length) {
      case 1: return t->value->uint8;
      case 2: return t->value->uint16;
      default: return (int32_t)t->value->uint32;
    }
  }
  return 0;
}

static int32_t find_int(DictionaryIterator *it, uint32_t key, int32_t fallback) {
  Tuple *t = dict_find(it, key);
  return t ? tuple_int(t) : fallback;
}

static void find_str(DictionaryIterator *it, uint32_t key, char *dst, size_t size) {
  Tuple *t = dict_find(it, key);
  if (t && t->type == TUPLE_CSTRING) {
    strncpy(dst, t->value->cstring, size - 1);
    dst[size - 1] = '\0';
  } else {
    dst[0] = '\0';
  }
}

static void handle_printer(DictionaryIterator *it) {
  int index = find_int(it, MESSAGE_KEY_Index, -1);
  if (index < 0 || index >= MAX_PRINTERS) {
    return;
  }
  Printer p;
  memset(&p, 0, sizeof(p));
  p.uid = (uint32_t)find_int(it, MESSAGE_KEY_Uid, 0);
  find_str(it, MESSAGE_KEY_Name, p.name, sizeof(p.name));
  find_str(it, MESSAGE_KEY_File, p.file, sizeof(p.file));
  find_str(it, MESSAGE_KEY_Message, p.message, sizeof(p.message));
  p.kind = find_int(it, MESSAGE_KEY_Kind, 0);
  p.state = find_int(it, MESSAGE_KEY_State, PS_UNKNOWN);
  p.flags = find_int(it, MESSAGE_KEY_Flags, 0);
  p.progress = find_int(it, MESSAGE_KEY_Progress, -1);
  p.nozzle = find_int(it, MESSAGE_KEY_Nozzle, TEMP_NONE);
  p.nozzle_target = find_int(it, MESSAGE_KEY_NozzleTarget, 0);
  p.bed = find_int(it, MESSAGE_KEY_Bed, TEMP_NONE);
  p.bed_target = find_int(it, MESSAGE_KEY_BedTarget, 0);
  p.layer = find_int(it, MESSAGE_KEY_Layer, -1);
  p.layer_total = find_int(it, MESSAGE_KEY_LayerTotal, -1);
  p.time_left = find_int(it, MESSAGE_KEY_TimeLeft, -1);
  p.elapsed = find_int(it, MESSAGE_KEY_Elapsed, -1);
  p.updated = find_int(it, MESSAGE_KEY_Stamp, (int32_t)time(NULL));
  model_put(index, &p);
}

static void handle_settings(DictionaryIterator *it) {
  Settings *s = model_settings();
  s->theme = find_int(it, MESSAGE_KEY_Theme, s->theme);
  s->vibe_on_done = find_int(it, MESSAGE_KEY_VibeOnDone, s->vibe_on_done);
  s->wake_on_done = find_int(it, MESSAGE_KEY_WakeOnDone, s->wake_on_done);
  s->interval = find_int(it, MESSAGE_KEY_Interval, s->interval);
  model_save_settings();
  app_on_settings_changed();
}

static void handle_notice(DictionaryIterator *it) {
  char text[48];
  find_str(it, MESSAGE_KEY_Message, text, sizeof(text));
  if (text[0]) {
    ui_toast(text, PBL_IF_COLOR_ELSE(GColorPictonBlue, theme()->text));
  }
}

static void inbox_received(DictionaryIterator *it, void *context) {
  switch (find_int(it, MESSAGE_KEY_Cmd, 0)) {
    case CMD_SYNC_BEGIN:
      app_on_sync_begin();
      break;
    case CMD_PRINTER:
      handle_printer(it);
      break;
    case CMD_SYNC_DONE:
      model_set_count(find_int(it, MESSAGE_KEY_Count, model_count()));
      app_on_sync_done();
      break;
    case CMD_SETTINGS:
      handle_settings(it);
      break;
    case CMD_NOTICE:
      handle_notice(it);
      break;
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Inbox dropped: %d", (int)reason);
}

static void outbox_failed(DictionaryIterator *it, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Outbox failed: %d", (int)reason);
}

void comm_init(void) {
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  app_message_open(640, 64);
}

void comm_request_refresh(void) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) {
    return;
  }
  dict_write_int32(it, MESSAGE_KEY_Cmd, CMD_REFRESH);
  app_message_outbox_send();
  app_on_sync_begin();
}

void comm_send_action(int index, int action) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) {
    ui_toast("Phone not connected", PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, theme()->text));
    return;
  }
  dict_write_int32(it, MESSAGE_KEY_Cmd, CMD_ACTION);
  dict_write_int32(it, MESSAGE_KEY_Index, index);
  dict_write_int32(it, MESSAGE_KEY_Action, action);
  app_message_outbox_send();
}

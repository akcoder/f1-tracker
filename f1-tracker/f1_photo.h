#pragma once
// A portrait that appears top-down as it decodes (copied in spirit from
// sky-tracker's UI-59f). The JPEG is in flash; a short-lived task on core 0 decodes
// it band by band into one reused PSRAM buffer and reports the rows done; a timer on
// the display loop redraws the picture while rows arrive. Rows not yet decoded are
// black, so nothing half-drawn is ever wrong. One of these per place a portrait is
// shown (the detail card at half size, the carousel card at full size).
#include <atomic>
#include <cstring>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "f1_jpg.h"
#include "f1_portraits.h"

namespace f1 {
namespace photo {

inline constexpr int PACE_MS = 14;     // between bands, so the reveal can be seen

struct Photo {
  int w = 0, h = 0;
  bool half = false;                   // 2x2 average of the portrait (else 1:1)
  lv_obj_t *img = nullptr;
  uint16_t *buf = nullptr;
  lv_image_dsc_t dsc{};
  lv_timer_t *timer = nullptr;
  std::atomic<int> rows{0};
  std::atomic<bool> running{false};
  volatile bool cancel = false;
  int shown = -1;
  const portraits::Portrait *p = nullptr;
};

inline void on_rows(int done, void *ctx) {
  static_cast<Photo *>(ctx)->rows.store(done);
  vTaskDelay(pdMS_TO_TICKS(PACE_MS));
}
inline void task(void *arg) {
  auto *ph = static_cast<Photo *>(arg);
  jpg::Progress pr;
  pr.rows = on_rows;
  pr.ctx = ph;
  pr.cancel = &ph->cancel;
  if (ph->half) jpg::decode_half(ph->p->jpeg, ph->p->len, ph->buf, ph->w, ph->h, &pr);
  else jpg::decode_full(ph->p->jpeg, ph->p->len, ph->buf, ph->w, ph->h, &pr);
  ph->running.store(false);
  vTaskDelete(nullptr);
}
inline void tick(lv_timer_t *t) {
  auto *ph = static_cast<Photo *>(lv_timer_get_user_data(t));
  const int r = ph->rows.load();
  if (r != ph->shown) { ph->shown = r; if (ph->img) lv_obj_invalidate(ph->img); }
  if (!ph->running.load() && r >= ph->h && ph->timer) { lv_timer_delete(ph->timer); ph->timer = nullptr; }
}

// Cancel a decode in progress and hide the picture.
inline void stop(Photo &ph) {
  ph.cancel = true;
  for (int i = 0; i < 40 && ph.running.load(); i++) vTaskDelay(pdMS_TO_TICKS(5));
  if (ph.timer) { lv_timer_delete(ph.timer); ph.timer = nullptr; }
  if (ph.img) lv_obj_add_flag(ph.img, LV_OBJ_FLAG_HIDDEN);
}

// Show `p` in ph.img, top-down. False = no picture (none, no memory, no task): the
// caller shows a text-only card.
inline bool start(Photo &ph, const portraits::Portrait *p) {
  stop(ph);
  if (p == nullptr || ph.img == nullptr) return false;
  if (ph.buf == nullptr)
    ph.buf = (uint16_t *) heap_caps_malloc((size_t) ph.w * ph.h * 2, MALLOC_CAP_SPIRAM);
  if (ph.buf == nullptr || ph.running.load()) return false;
  std::memset(ph.buf, 0, (size_t) ph.w * ph.h * 2);
  ph.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  ph.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  ph.dsc.header.w = ph.w;
  ph.dsc.header.h = ph.h;
  ph.dsc.header.stride = ph.w * 2;
  ph.dsc.data_size = (uint32_t) ph.w * ph.h * 2;
  ph.dsc.data = (const uint8_t *) ph.buf;
  lv_image_set_src(ph.img, &ph.dsc);
  lv_obj_remove_flag(ph.img, LV_OBJ_FLAG_HIDDEN);
  ph.p = p;
  ph.cancel = false;
  ph.rows.store(0);
  ph.shown = -1;
  ph.running.store(true);
  ph.timer = lv_timer_create(tick, 40, &ph);
  if (xTaskCreatePinnedToCore(task, "f1jpg", 10240, &ph, 1, nullptr, 0) != pdPASS) {
    ph.running.store(false);
    stop(ph);
    return false;
  }
  return true;
}

}  // namespace photo
}  // namespace f1

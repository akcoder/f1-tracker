#pragma once
// Headshots fetched at runtime, for a driver with no compiled portrait (a rookie who joined
// after the firmware was built, RACE-13e). The compiled-in portraits stay the main source:
// reviewed, credited, offline. This fills the gap.
//
// The picture is the one OpenF1 links for the driver (media.formula1.com, 206 px square,
// ~15 KB, a palette PNG with transparency). It is decoded AS IT DOWNLOADS (f1_png.h) into a
// PSRAM buffer, composited over black, and the card shows the rows as they arrive. A few
// decoded pictures are kept (four, least recently used out) so a card seen twice is not
// downloaded twice; nothing is written to flash, and a reboot starts empty.
//
// Licence: the picture is the F1 site's, not ours. Nothing here stores it in the repository
// or ships it in the firmware; the device fetches it for the owner's own display
// (decision 193).
//
// Threads: the UI thread asks (want) and reads; the network task fetches and writes. The
// state and row count are atomics; the buffer is only written while LOADING.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef F1_HOST_TEST
#include "esp_heap_caps.h"
#endif

namespace f1 {
namespace headshot {

inline constexpr int W = 206, H = 206;             // the 2col size
inline constexpr int SLOTS = 4;
inline constexpr uint32_t RETRY_MS = 10 * 60 * 1000;   // a failed fetch waits this long

enum State : int { EMPTY = 0, LOADING = 1, READY = 2, FAILED = 3 };

struct Slot {
  char code[8] = {0};
  std::atomic<int> state{EMPTY};
  std::atomic<int> rows{0};                        // output rows decoded so far (top-down)
  std::atomic<bool> queued{false};                 // waiting for the network task
  uint16_t *buf = nullptr;                         // W x H RGB565
  uint32_t used_ms = 0;                            // last asked for
  uint32_t failed_ms = 0;
  int pins = 0;                                    // pictures on screen reading buf (UI thread only)
};

inline Slot g_slots[SLOTS];

inline Slot *find(const char *code) {
  for (auto &s : g_slots)
    if (s.state.load() != EMPTY && std::strcmp(s.code, code) == 0) return &s;
  return nullptr;
}

// UI thread: the picture for `code`, asking for it if it is not here. nullptr = no picture
// (no memory, or a recent failure).
inline Slot *want(const char *code, uint32_t now_ms) {
  if (code == nullptr || !*code) return nullptr;
  Slot *s = find(code);
  if (s != nullptr) {
    s->used_ms = now_ms;
    if (s->state.load() == FAILED) {
      if ((uint32_t) (now_ms - s->failed_ms) < RETRY_MS) return nullptr;
      s->rows.store(0);
      s->state.store(LOADING);                     // try again
      s->queued.store(true);
    }
    return s;
  }
  // a free slot, else the least recently used one that is neither being fetched nor on screen
  Slot *pick = nullptr;
  for (auto &c : g_slots) {
    if (c.state.load() == EMPTY) { pick = &c; break; }
    if (c.state.load() == LOADING || c.pins > 0) continue;
    if (pick == nullptr || (int32_t) (c.used_ms - pick->used_ms) < 0) pick = &c;
  }
  if (pick == nullptr) return nullptr;             // all four are mid-fetch or showing
  if (pick->buf == nullptr) {
#ifdef F1_HOST_TEST
    pick->buf = (uint16_t *) std::malloc((size_t) W * H * 2);
#else
    pick->buf = (uint16_t *) heap_caps_malloc((size_t) W * H * 2, MALLOC_CAP_SPIRAM);
#endif
    if (pick->buf == nullptr) return nullptr;
  }
  std::memset(pick->buf, 0, (size_t) W * H * 2);
  std::snprintf(pick->code, sizeof(pick->code), "%s", code);
  pick->rows.store(0);
  pick->used_ms = now_ms;
  pick->state.store(LOADING);
  pick->queued.store(true);
  return pick;
}

// UI thread: a picture being shown from `s.buf` must not be recycled under it (f1_photo.h
// pins while it shows, unpins when it stops).
inline void pin(Slot &s) { s.pins++; }
inline void unpin(Slot &s) { if (s.pins > 0) s.pins--; }

// Network task: the next picture waiting to be fetched, or nullptr.
inline Slot *next_job() {
  for (auto &s : g_slots)
    if (s.state.load() == LOADING && s.queued.load()) return &s;
  return nullptr;
}

inline void finish(Slot &s, bool ok, uint32_t now_ms) {
  s.queued.store(false);
  if (ok) { s.rows.store(H); s.state.store(READY); }
  else { s.failed_ms = now_ms; s.state.store(FAILED); }
}

// Is a picture waiting that the URL list may not yet cover? (drives the one OpenF1 request)
inline bool list_wanted(int n_known) {
  return n_known == 0 && next_job() != nullptr;
}

}  // namespace headshot
}  // namespace f1

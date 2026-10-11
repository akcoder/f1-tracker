#pragma once
// The boot / Wi-Fi status page's behaviour (NET-2b, NET-2c, BOOT-2): the state machine is
// shared (tracker_boot.h); what is F1's own is which page a connect lands on.
#include "f1_state.h"

#if __has_include("esphome/components/tracker_common/tracker_boot.h")
#include "esphome/components/tracker_common/tracker_boot.h"
#else
#include "tracker_boot.h"             // host tests: -I the tracker-common component dir
#endif

namespace f1 {
namespace boot {

using namespace tracker::boot;

// UI-2a: the page a connect lands on is whichever the state machine calls
// primary (6.1) - the race page around a session, the carousel otherwise.
inline bool race_is_primary(state::Weekend w) {
  return w == state::SESSION_SOON || w == state::SESSION_LIVE ||
         w == state::RACE_LIVE || w == state::POST_SESSION;
}

inline Step step(Tracker &t, Link link, uint32_t now_ms, bool on_status,
                 bool settings_open, state::Weekend weekend) {
  return tracker::boot::step(t, link, now_ms, on_status, settings_open, race_is_primary(weekend));
}

}  // namespace boot
}  // namespace f1

#pragma once
// The watched driver (section 6.14). Ships watching Max Verstappen.
//
// Free of LVGL so the latch can be tested on the host - decision 80 calls that
// the most likely bug in the feature, because a mid-weekend reboot must not
// replay the whole set of alerts.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "f1_state.h"

namespace f1 {
namespace watch {

// 6.14.1 / DATA-6: the watched driver is a driverId, NEVER a car number.
// Verstappen carries 3 in 2026 while Norris carries 1; a number-keyed watch
// would have followed Norris this season and someone else the next.
inline constexpr const char *DEFAULT_DRIVER_ID = "max_verstappen";

enum Tier : uint8_t { NONE = 0, AMBIENT, EVENT, MILESTONE };

enum Event : uint8_t {
  EV_NONE = 0,
  EV_RACING_TODAY,    // race day, he is entered
  EV_ON_TRACK,        // a session window is open and he is in it
  EV_GRID_SET,        // qualifying classification exists
  EV_RESULT,          // the window closed and the result is readable
  EV_MILESTONE,       // a win, a round number, a title
  EV_N
};

// 6.14.3: three tiers, deliberately louder than the sibling projects allow
// (decision 92). Safe HERE specifically because nothing underneath is changing
// - there is no live timing - so a banner costs the reader nothing. That is
// not a general licence.
inline Tier tier_of(Event e) {
  switch (e) {
    case EV_MILESTONE:    return MILESTONE;
    case EV_RACING_TODAY:
    case EV_ON_TRACK:
    case EV_GRID_SET:
    case EV_RESULT:       return EVENT;
    default:              return NONE;
  }
}

// Hold times. A tap dismisses early; nothing ever REQUIRES dismissing.
inline constexpr uint32_t EVENT_HOLD_MS = 30000;
inline constexpr uint32_t TAKEOVER_MS = 8000;

// decision 80: each event fires once per (round, session, event). The latch is
// restored across a reboot, so a mid-weekend restart does not replay the set.
//
// It must be 64 bits, not 32. There are N_SESSIONS (7) sessions and EV_N (6)
// events, so the index reaches 7*6 = 42 - and a 32-bit word silently dropped
// every bit from index 32 up, which is the whole of the RACE session. The
// effect was that race alerts never latched and fired on every tick; the host
// test caught it immediately.
struct Latch {
  uint16_t season = 0;
  uint8_t round = 0;
  uint64_t fired = 0;      // bit per (session * EV_N + event)

  static constexpr int BITS_NEEDED = (int) calendar::N_SESSIONS * (int) EV_N;
  static_assert(BITS_NEEDED <= 64, "latch word too small for sessions x events");

  void reset_if_changed(uint16_t s, uint8_t r) {
    if (s != season || r != round) { season = s; round = r; fired = 0; }
  }
  static uint64_t bit(int session, Event e) {
    if (session < 0 || session >= (int) calendar::N_SESSIONS) return 0;
    const int idx = session * (int) EV_N + (int) e;
    return (idx >= 0 && idx < 64) ? (1ull << idx) : 0ull;
  }
  // An out-of-range session yields bit 0, which would make already() return
  // false forever. Report it as ALREADY fired instead, so an unlatchable event
  // is suppressed rather than repeated every tick.
  bool already(int session, Event e) const {
    const uint64_t b = bit(session, e);
    return b == 0 || (fired & b) != 0;
  }
  void mark(int session, Event e) { fired |= bit(session, e); }
};

struct Alert {
  Event event = EV_NONE;
  Tier tier = NONE;
  char line[64] = {0};
  uint32_t raised_ms = 0;
  bool active = false;
};

struct Config {
  char driver_id[32] = {0};
  char code[8] = {0};        // 3-letter acronym for the ambient marker
  char given[24] = {0};      // as given, NOT upper-cased: it appears in a sentence
  char iso3[4] = {0};
  bool enabled = true;
  bool milestones_only = false;
  bool loud = true;          // decision 92: loud ships as the default
  bool entered = false;      // is he in the current entry list?
  uint16_t career_wins = 0;
  uint16_t career_starts = 0;
};

// A milestone is rare by construction: a win, a round-number win or start, or a
// title. If this fires twice in a weekend the latch is broken.
inline bool is_milestone(int finish_pos, uint16_t wins_after, uint16_t starts_after,
                         bool title, char *out, size_t n) {
  if (title) { std::snprintf(out, n, "World champion"); return true; }
  if (finish_pos == 1) {
    if (wins_after % 10 == 0) std::snprintf(out, n, "%uth win", wins_after);
    else std::snprintf(out, n, "Wins");
    return true;
  }
  if (starts_after > 0 && starts_after % 50 == 0) {
    std::snprintf(out, n, "%uth start", starts_after);
    return true;
  }
  return false;
}

// Decide what, if anything, to raise. Returns EV_NONE when nothing is due.
// `finish_pos` is 0 when no result is known yet.
// Returns the event AND the session it is latched against, so step() can mark
// exactly what due() tested. Computing the session twice, slightly differently,
// is how the latch leaked in the first place.
inline Event due(const Config &cfg, const state::Status &st, const Latch &latch,
                 int finish_pos, int &session_out) {
  session_out = -1;
  if (!cfg.enabled || !cfg.entered || !st.clock_valid) return EV_NONE;

  const int cur = st.current.valid() ? st.current.session : -1;

  // Result first: it is the most informative thing we can say.
  if (st.weekend == state::POST_SESSION && cur >= 0 &&
      !latch.already(cur, EV_RESULT)) {
    session_out = cur;
    return EV_RESULT;
  }

  if (cfg.milestones_only) return EV_NONE;

  if ((st.weekend == state::RACE_LIVE || st.weekend == state::SESSION_LIVE) &&
      cur >= 0 && !latch.already(cur, EV_ON_TRACK)) {
    session_out = cur;
    return EV_ON_TRACK;
  }

  if (st.weekend == state::SESSION_SOON && st.next.valid() &&
      state::is_race(st.next.session) &&
      !latch.already(st.next.session, EV_RACING_TODAY)) {
    session_out = st.next.session;
    return EV_RACING_TODAY;
  }

  // The grid is set once per ROUND, and it is latched against the qualifying
  // session whether or not qualifying is the session we are currently inside.
  if (st.order == state::GRID_PROVISIONAL &&
      !latch.already((int) calendar::QUALI, EV_GRID_SET)) {
    session_out = (int) calendar::QUALI;
    return EV_GRID_SET;
  }

  return EV_NONE;
}

// The line shown. Kept here rather than scattered through the drawing code so
// the wording can be changed in one place (6.14.3).
inline void compose(const Config &cfg, Event e, const state::Status &st,
                    int grid_pos, int finish_pos, const char *status,
                    char *out, size_t n) {
  const char *who = cfg.given[0] ? cfg.given : cfg.code;
  switch (e) {
    case EV_RACING_TODAY:
      std::snprintf(out, n, "%s is racing today", who);
      break;
    case EV_ON_TRACK:
      std::snprintf(out, n, "%s is on track \u00b7 %s", who,
                    st.current.valid() ? state::session_label(st.current.session)
                                       : "session");
      break;
    case EV_GRID_SET:
      if (grid_pos == 1) std::snprintf(out, n, "%s takes pole", who);
      else if (grid_pos > 0) std::snprintf(out, n, "%s starts P%d", who, grid_pos);
      else std::snprintf(out, n, "%s \u00b7 grid set", who);
      break;
    case EV_RESULT:
      // 6.14.2: a withdrawal is NOT a result. Say nothing rather than
      // inventing a DNF.
      if (finish_pos == 1) std::snprintf(out, n, "%s wins", who);
      else if (finish_pos > 0) std::snprintf(out, n, "%s finishes P%d", who, finish_pos);
      else if (status && *status) std::snprintf(out, n, "%s \u00b7 %s", who, status);
      else out[0] = '\0';
      break;
    default:
      out[0] = '\0';
      break;
  }
}

// One step of the alert machine. Call it whenever the state is re-evaluated.
inline bool step(const Config &cfg, const state::Status &st, Latch &latch,
                 Alert &alert, uint32_t now_ms, uint16_t season, uint8_t round,
                 int grid_pos, int finish_pos, const char *status) {
  latch.reset_if_changed(season, round);

  // Retire an alert that has served its time. Nothing ever REQUIRES dismissing.
  if (alert.active) {
    const uint32_t hold = (alert.tier == MILESTONE) ? TAKEOVER_MS : EVENT_HOLD_MS;
    if (now_ms - alert.raised_ms >= hold) alert.active = false;
  }

  int session = -1;
  const Event e = due(cfg, st, latch, finish_pos, session);
  if (e == EV_NONE) return false;

  char line[64];
  compose(cfg, e, st, grid_pos, finish_pos, status, line, sizeof(line));
  // 6.14.2: nothing to say is a valid outcome - mark it fired so we do not
  // re-evaluate it every tick, but raise nothing.
  latch.mark(session, e);
  if (line[0] == '\0') return false;

  alert.event = e;
  alert.tier = tier_of(e);
  std::snprintf(alert.line, sizeof(alert.line), "%s", line);
  alert.raised_ms = now_ms;
  alert.active = true;
  return true;
}

inline void dismiss(Alert &alert) { alert.active = false; }

}  // namespace watch
}  // namespace f1

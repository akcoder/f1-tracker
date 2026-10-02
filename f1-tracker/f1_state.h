#pragma once
// The weekend state machine (RACE-1, section 4.1) and the OpenF1 live-window
// arithmetic (NET-14, 3.6.1).
//
// Everything the device shows is a function of ONE state derived from the
// calendar and the clock. Free of LVGL and ESPHome so tests/ can drive it with
// synthetic clocks - which matters more here than anywhere else in the project,
// because the bug section 4.1 warns about is invisible until you are in the
// wrong time zone at the wrong hour.
#include <cstdint>
#include <cstring>

#include "f1_calendar.h"

namespace f1 {
namespace state {

using calendar::N_SESSIONS;
using calendar::Round;

enum Weekend : uint8_t {
  OFF_SEASON,     // no future round in the calendar
  IDLE,           // next session more than 24 h away
  RACE_WEEK,      // a session of the next round within 7 days
  SESSION_SOON,   // a session starts within 2 h
  SESSION_LIVE,   // inside a non-race session
  RACE_LIVE,      // inside the Race (or the Sprint - RACE-14, decision 90)
  POST_SESSION,   // the OpenF1 window has closed and the result is readable
  N_WEEKEND
};

inline const char *weekend_name(Weekend w) {
  switch (w) {
    case OFF_SEASON:   return "OFF_SEASON";
    case IDLE:         return "IDLE";
    case RACE_WEEK:    return "RACE_WEEK";
    case SESSION_SOON: return "SESSION_SOON";
    case SESSION_LIVE: return "SESSION_LIVE";
    case RACE_LIVE:    return "RACE_LIVE";
    case POST_SESSION: return "POST_SESSION";
    default:           return "?";
  }
}

// 6.3 RACE-12. The mode's name is always on screen so the order is never
// ambiguous. There is no RUNNING mode - that needs OpenF1's paid tier
// (decision 61).
enum OrderMode : uint8_t { ENTRY_LIST, GRID_PROVISIONAL, GRID, FINAL };

// Sentence case. Shouting every label is a habit, not a design: when EVERYTHING
// is capitalised nothing is emphasised, and long runs of capitals are measurably
// slower to read because the word-shape cue disappears. Capitals are kept for
// the two places they earn it - driver surnames, which is the timing-screen
// convention and aids scanning down a monospace column, and short tag labels.
inline const char *order_name(OrderMode m) {
  switch (m) {
    case ENTRY_LIST:       return "Entry list";
    case GRID_PROVISIONAL: return "Grid (provisional)";
    case GRID:             return "Grid";
    case FINAL:            return "Final";
    default:               return "?";
  }
}

// ---- constants, all in seconds ------------------------------------------
inline constexpr int32_t LIVE_PAD    = 30 * 60;   // OpenF1's window, either side
inline constexpr int32_t SESSION_LEN = 2 * 3600;  // assumed non-race session length
inline constexpr int32_t RACE_LEN    = 3 * 3600;  // 4.1: bound RACE_LIVE at 3 h
inline constexpr int32_t SPRINT_LEN  = 1 * 3600;
inline constexpr int32_t SOON        = 2 * 3600;
inline constexpr int32_t IDLE_GAP    = 24 * 3600;
inline constexpr int32_t RACE_WEEK_GAP = 7 * 24 * 3600;
inline constexpr int32_t POST_HOLD   = 12 * 3600;

inline int32_t session_len(int s) {
  if (s == calendar::RACE) return RACE_LEN;
  if (s == calendar::SPRINT) return SPRINT_LEN;
  return SESSION_LEN;
}

// RACE-14 / decision 90: a Sprint is a first-class race day, so both of these
// drive RACE_LIVE and get the full race page.
inline bool is_race(int s) { return s == calendar::RACE || s == calendar::SPRINT; }

struct Session {
  int round_idx = -1;
  int session = -1;
  uint32_t start = 0;
  bool valid() const { return round_idx >= 0 && start != 0; }
  uint32_t end() const { return start + (uint32_t) session_len(session); }
  // NET-14: the paid window. The device computes this and never requests
  // inside it, which is strictly better than a request that cannot be
  // rate-limited, mistaken for a fault, or return a stale snapshot.
  uint32_t window_opens() const { return start - (uint32_t) LIVE_PAD; }
  uint32_t window_closes() const { return end() + (uint32_t) LIVE_PAD; }
};

inline const char *session_label(int s) {
  switch (s) {
    case calendar::FP1:    return "FP1";       // an abbreviation, not shouting
    case calendar::FP2:    return "FP2";
    case calendar::FP3:    return "FP3";
    case calendar::SQ:     return "Sprint qualifying";
    case calendar::SPRINT: return "Sprint";
    case calendar::QUALI:  return "Qualifying";
    case calendar::RACE:   return "Race";
    default:               return "Session";
  }
}

// A calendar the state machine can read: either the compiled floor or a
// fetched one (DATA-3). Rounds must be in chronological order.
struct Calendar {
  const Round *rounds = calendar::R;
  int n = calendar::N_ROUNDS;
  uint16_t season = calendar::SEASON;
  bool fetched = false;    // false = the compiled floor
};

struct Status {
  Weekend weekend = OFF_SEASON;
  OrderMode order = ENTRY_LIST;
  Session current;   // the session we are inside, if any
  Session next;      // the next session that has not started
  int32_t to_next = 0;        // seconds until next.start, 0 when unknown
  int32_t to_results = 0;     // seconds until current.window_closes()
  bool clock_valid = false;
};

// The session that most recently started at or before `now`, and the next one
// after it. Sessions with start == 0 are UNKNOWN, not midnight (section 3.4),
// and are skipped entirely.
inline void scan(const Calendar &cal, uint32_t now, Session &cur, Session &nxt) {
  cur = Session();
  nxt = Session();
  for (int i = 0; i < cal.n; i++) {
    const Round &r = cal.rounds[i];
    for (int s = 0; s < N_SESSIONS; s++) {
      const uint32_t st = r.start[s];
      if (st == 0) continue;
      if (st <= now) {
        Session c; c.round_idx = i; c.session = s; c.start = st;
        if (now < c.end() && (!cur.valid() || st > cur.start)) cur = c;
      } else if (!nxt.valid() || st < nxt.start) {
        nxt.round_idx = i; nxt.session = s; nxt.start = st;
      }
    }
  }
}

// The most recently ENDED session, used for POST_SESSION.
inline Session last_ended(const Calendar &cal, uint32_t now) {
  Session best;
  for (int i = 0; i < cal.n; i++) {
    const Round &r = cal.rounds[i];
    for (int s = 0; s < N_SESSIONS; s++) {
      const uint32_t st = r.start[s];
      if (st == 0) continue;
      Session c; c.round_idx = i; c.session = s; c.start = st;
      if (c.end() <= now && (!best.valid() || c.start > best.start)) best = c;
    }
  }
  return best;
}

// 4.1: state is derived from session timestamps in UTC, NEVER from the
// device's local calendar date. A race starting 04:00Z in Melbourne is the
// previous evening in Alaska, so a local-date test would show the race-day
// screen on the wrong day for roughly half the calendar.
inline Status evaluate(const Calendar &cal, uint32_t now_utc, bool clock_valid) {
  Status st;
  st.clock_valid = clock_valid;
  if (!clock_valid || cal.n <= 0) {
    // An unsynced clock must suppress the state machine rather than let it
    // guess (6.7). OFF_SEASON is the safe resting state and the carousel runs
    // regardless (4.3).
    st.weekend = OFF_SEASON;
    st.order = ENTRY_LIST;
    return st;
  }

  scan(cal, now_utc, st.current, st.next);
  st.to_next = st.next.valid() ? (int32_t) (st.next.start - now_utc) : 0;

  if (st.current.valid()) {
    st.weekend = is_race(st.current.session) ? RACE_LIVE : SESSION_LIVE;
    st.to_results = (int32_t) (st.current.window_closes() - now_utc);
  } else {
    const Session ended = last_ended(cal, now_utc);
    const bool recent = ended.valid() &&
                        (now_utc - ended.end()) <= (uint32_t) POST_HOLD;
    if (recent && now_utc >= ended.window_closes()) {
      // decision 60: POST_SESSION begins when the WINDOW closes, not when the
      // session ends. That is the moment the data becomes readable, and the
      // one transition worth getting exactly right.
      st.weekend = POST_SESSION;
      st.current = ended;
    } else if (recent) {
      // ended, but still inside the paid window: nothing is readable yet
      st.weekend = is_race(ended.session) ? RACE_LIVE : SESSION_LIVE;
      st.current = ended;
      st.to_results = (int32_t) (ended.window_closes() - now_utc);
    } else if (!st.next.valid()) {
      st.weekend = OFF_SEASON;
    } else if (st.to_next <= SOON) {
      st.weekend = SESSION_SOON;
    } else if (st.to_next <= IDLE_GAP) {
      st.weekend = RACE_WEEK;
    } else if (st.to_next <= RACE_WEEK_GAP) {
      st.weekend = RACE_WEEK;
    } else {
      st.weekend = IDLE;
    }
  }

  // ---- order mode (RACE-12). Qualifying classification is PROVISIONAL until
  // the race result exists, because penalties are not applied to it.
  const int ridx = st.current.valid() ? st.current.round_idx
                                      : (st.next.valid() ? st.next.round_idx : -1);
  if (ridx < 0) {
    st.order = ENTRY_LIST;
  } else {
    const Round &r = cal.rounds[ridx];
    const uint32_t quali = r.start[calendar::QUALI];
    const uint32_t race = r.start[calendar::RACE];
    Session rs; rs.round_idx = ridx; rs.session = calendar::RACE; rs.start = race;
    if (race && now_utc >= rs.window_closes())      st.order = FINAL;
    else if (quali && now_utc >= quali + SESSION_LEN) st.order = GRID_PROVISIONAL;
    else                                            st.order = ENTRY_LIST;
  }
  return st;
}

// NET-14: may we fetch OpenF1 for this session right now? The device computes
// the window and skips, rather than making a request that will not answer.
inline bool openf1_readable(const Session &s, uint32_t now_utc) {
  if (!s.valid()) return false;
  return now_utc < s.window_opens() || now_utc >= s.window_closes();
}

// Any session of any round currently inside its paid window blocks OpenF1
// entirely - we never want to touch the API during one.
inline bool any_window_open(const Calendar &cal, uint32_t now_utc) {
  for (int i = 0; i < cal.n; i++) {
    const Round &r = cal.rounds[i];
    for (int s = 0; s < N_SESSIONS; s++) {
      if (r.start[s] == 0) continue;
      Session c; c.round_idx = i; c.session = s; c.start = r.start[s];
      if (now_utc >= c.window_opens() && now_utc < c.window_closes()) return true;
    }
  }
  return false;
}

// ---- poll intervals, one table rather than inline logic (3.6) ------------
struct Intervals { int32_t jolpica_s; int32_t openf1_s; };

inline Intervals intervals_for(Weekend w) {
  switch (w) {
    case OFF_SEASON:   return {24 * 3600, 0};
    case IDLE:         return {6 * 3600, 0};
    case RACE_WEEK:    return {3600, 0};
    case SESSION_SOON: return {15 * 60, 0};
    case SESSION_LIVE: return {30 * 60, 0};   // inside the paid window: nothing
    case RACE_LIVE:    return {30 * 60, 0};   // to fetch (NET-14)
    case POST_SESSION: return {10 * 60, 5 * 60};
    default:           return {6 * 3600, 0};
  }
}

}  // namespace state
}  // namespace f1

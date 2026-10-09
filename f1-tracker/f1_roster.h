#pragma once
// Who is racing THIS season, and what the driver carousel does about it
// (RACE-13d/13e, decisions 96-98). LVGL-free so tests/ can build it.
//
// The compiled driver table (f1_drivers.h) is a snapshot of one season. The
// device is meant to sit on a wall for years, so "is this driver current?" is
// answered by the live roster whenever one has been fetched, and by the compiled
// table only as the floor before the first fetch:
//
//   in the roster, in the compiled table   -> driver card (LEGEND badge if one)
//   in the compiled table, NOT the roster  -> no driver card; the legend card
//                                             covers them if they are one
//   in the roster, NOT the compiled table  -> a text-only driver card (a rookie)
//
// Baking any of this at build time would make a driver who retires over the
// winter vanish from both rotations, which is what decision 96 corrected.
#include <cstring>

#include "f1_drivers.h"
#include "f1_store.h"

namespace f1 {
namespace roster {

// What the UI keeps: a copy of the roster rows, so the carousel never reads the
// store from the UI thread without the lock.
struct View {
  int n = 0;
  store::RosterDriver rows[store::MAX_ROSTER];
  uint16_t season = 0;
  bool live() const { return n > 0; }
};

inline void build(const store::Store &st, View &v) {
  v.n = st.n_roster;
  v.season = st.roster_season;
  for (int i = 0; i < store::MAX_ROSTER; i++) v.rows[i] = st.roster[i];
}

// Same roster? Compared by identity, not bytes: the rows carry padding, and a
// spurious "changed" would reset the staged card every tick.
inline bool same(const View &a, const View &b) {
  if (a.n != b.n || a.season != b.season) return false;
  for (int i = 0; i < a.n; i++)
    if (std::strcmp(a.rows[i].id, b.rows[i].id) != 0) return false;
  return true;
}

inline bool in_compiled(const char *driver_id) {
  for (int i = 0; i < drivers::N; i++)
    if (std::strcmp(drivers::P[i].driver_id, driver_id) == 0) return true;
  return false;
}

// Is this driver racing now? The roster when there is one; otherwise the
// compiled floor, so a cold boot with no network still behaves.
inline bool racing(const View &v, const char *driver_id) {
  if (!driver_id || !*driver_id) return false;
  if (!v.live()) return in_compiled(driver_id);
  for (int i = 0; i < v.n; i++)
    if (std::strcmp(v.rows[i].id, driver_id) == 0) return true;
  return false;
}

// Roster rows with no compiled profile - a rookie who joined after the build
// (RACE-13e). They get a text-only card from what the roster knows.
inline int live_only_count(const View &v) {
  int k = 0;
  for (int i = 0; i < v.n; i++) if (!in_compiled(v.rows[i].id)) k++;
  return k;
}

inline const store::RosterDriver *live_only(const View &v, int k) {
  for (int i = 0; i < v.n; i++) {
    if (in_compiled(v.rows[i].id)) continue;
    if (k-- == 0) return &v.rows[i];
  }
  return nullptr;
}

// How many driver slots the rotation walks: every compiled profile (those no
// longer racing are skipped when drawn) plus the rookies after them.
inline int driver_slots(const View &v) { return drivers::N + live_only_count(v); }

// Is the watched driver entered this season (UI-50, DATA-6)? The watch is keyed
// on driverId, so it follows the person - but a driver who has left the grid
// must stop raising "is racing today" alerts. Before a roster exists the answer
// is whatever it was (the device ships watching Verstappen, entered).
inline bool watched_entered(const View &v, const char *driver_id, bool previous) {
  return v.live() ? racing(v, driver_id) : previous;
}

enum SlotKind : uint8_t { SLOT_NONE, SLOT_COMPILED, SLOT_RETIRED, SLOT_ROOKIE };

inline SlotKind slot_kind(const View &v, int idx) {
  if (idx < 0) return SLOT_NONE;
  if (idx < drivers::N)
    return racing(v, drivers::P[idx].driver_id) ? SLOT_COMPILED : SLOT_RETIRED;
  return live_only(v, idx - drivers::N) ? SLOT_ROOKIE : SLOT_NONE;
}

}  // namespace roster
}  // namespace f1

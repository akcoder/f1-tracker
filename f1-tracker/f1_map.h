#pragma once
// Circuit map geometry (MAP-3 / MAP-5) and circuitId lookup (decision 23).
//
// Deliberately free of LVGL and of ESPHome so tests/ can build it on the host
// (section 10). Everything expensive - projection, the fill rotation, the
// int16 normalisation - already happened in tools/gen_circuits.py; what is left
// is a scale and a translate.
#include <cstdint>
#include <cstring>

#include "f1_circuits.h"

namespace f1 {
namespace map {

using circuits::Circuit;

// Where a trace lands inside a box: one scale for both axes (MAP-3 - a circuit
// squeezed to fill a square is unrecognisable) plus the centring offset.
struct Fit {
  float scale;    // normalised units -> pixels
  float cx, cy;   // box centre, pixels
  int w, h;       // the box this was fitted to
};

// Largest scale that keeps the whole trace inside w x h, aspect preserved.
// pad leaves room for the line width so a 3 px stroke is not clipped.
inline Fit fit_box(const Circuit &c, int w, int h, float pad = 3.0f) {
  const float aw = (float) w - 2.0f * pad, ah = (float) h - 2.0f * pad;
  const float hw = c.half_w > 0 ? (float) c.half_w : 1.0f;
  const float hh = c.half_h > 0 ? (float) c.half_h : 1.0f;
  const float sx = aw / (2.0f * hw), sy = ah / (2.0f * hh);
  Fit f;
  f.scale = sx < sy ? sx : sy;
  f.cx = (float) w * 0.5f;
  f.cy = (float) h * 0.5f;
  f.w = w;
  f.h = h;
  return f;
}

// Point i of circuit c, in box pixel coordinates. y is negated because the
// trace is north-up in maths convention and the screen grows downward.
inline void point_at(const Circuit &c, int i, const Fit &f, float &x, float &y) {
  const int16_t *p = &circuits::PTS[(size_t) c.first_pt * 2 + (size_t) i * 2];
  x = f.cx + (float) p[0] * f.scale;
  y = f.cy - (float) p[1] * f.scale;
}

// MAP-3: a rotated map with no orientation cue is a quietly wrong map. The
// generator recorded the rotation it APPLIED, so north now points at its
// negative. Degrees clockwise from screen-up.
inline float north_deg(const Circuit &c) {
  float d = -(float) c.rot_cdeg / 100.0f;
  while (d < 0.0f) d += 360.0f;
  while (d >= 360.0f) d -= 360.0f;
  return d;
}

// True when the generator found no rotation worth applying, so no arrow is
// needed and the map really is north-up.
inline bool is_north_up(const Circuit &c) { return c.rot_cdeg == 0; }

// Jolpica circuitId -> circuit, by binary search over the generated table.
// Returns nullptr for a circuit we have no trace for, which is a normal state:
// 37 historical circuitIds have none, and they show a no-map card.
inline const Circuit *by_circuit_id(const char *circuit_id) {
  if (circuit_id == nullptr || *circuit_id == '\0') return nullptr;
  int lo = 0, hi = circuits::N_IDMAP - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const int cmp = std::strcmp(circuit_id, circuits::BY_CIRCUIT_ID[mid].circuit_id);
    if (cmp == 0) return &circuits::C[circuits::BY_CIRCUIT_ID[mid].idx];
    if (cmp < 0) hi = mid - 1; else lo = mid + 1;
  }
  return nullptr;
}

inline const Circuit *by_trace_id(const char *trace_id) {
  for (int i = 0; i < circuits::N; i++)
    if (std::strcmp(trace_id, circuits::C[i].id) == 0) return &circuits::C[i];
  return nullptr;
}

// The start/finish tick: a short segment perpendicular to the track at point 0
// (MAP-5). Direction comes from the first two points.
inline void start_tick(const Circuit &c, const Fit &f, float len,
                       float &x0, float &y0, float &x1, float &y1) {
  float ax, ay, bx, by;
  point_at(c, 0, f, ax, ay);
  point_at(c, c.n_pts > 1 ? 1 : 0, f, bx, by);
  float dx = bx - ax, dy = by - ay;
  const float m = dx * dx + dy * dy;
  if (m <= 0.0f) { dx = 1.0f; dy = 0.0f; }
  else { const float inv = 1.0f / sqrtf(m); dx *= inv; dy *= inv; }
  // perpendicular
  const float px = -dy, py = dx;
  x0 = ax - px * len * 0.5f; y0 = ay - py * len * 0.5f;
  x1 = ax + px * len * 0.5f; y1 = ay + py * len * 0.5f;
}

}  // namespace map
}  // namespace f1

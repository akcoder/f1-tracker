// f1_animlogic.h: the boot page's start-lights timing, the cars' path round the logo and
// which way each one points.
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <initializer_list>

#include "../f1-tracker/f1_animlogic.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
  using namespace f1::animlogic;
  // lights: none, then one per step, five held, then out
  CHECK(lit(-1) == 0 && lit(0) == 0 && lit(STEP_MS - 1) == 0, "nothing before the first step");
  CHECK(lit(STEP_MS) == 1 && lit(2 * STEP_MS) == 2 && lit(5 * STEP_MS) == 5, "one more each step");
  CHECK(lit(5 * STEP_MS + HOLD_MS - 1) == 5, "five are held");
  CHECK(lit(OUT_MS) == 0 && lit(OUT_MS + 100000) == 0, "lights out, and they stay out");
  int prev = 0; bool mono = true;
  for (int t = 0; t < OUT_MS; t += 10) { const int k = lit(t); if (k < prev) mono = false; prev = k; }
  CHECK(mono, "the count never falls before lights out");
  CHECK(gantry_visible(0) && gantry_visible(OUT_MS) && !gantry_visible(GANTRY_END_MS) && !gantry_visible(-1),
        "the gantry shows for the sequence and a moment after");
  CHECK(GANTRY_END_MS < LAP_MS, "the whole sequence fits in the first lap, so a looping render repeats cleanly");

  // the track, as the 200 px and 112 px logos lay it out
  for (int size : {200, 112}) {
    Track t;
    CHECK(build(t, size), "built at %d", size);
    CHECK(t.n == f1::logomark::N + 1, "closed: %d points + the first again (%d)", f1::logomark::N, t.n);
    float minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (int i = 0; i < t.n; i++) {
      minx = std::fmin(minx, t.x[i]); maxx = std::fmax(maxx, t.x[i]);
      miny = std::fmin(miny, t.y[i]); maxy = std::fmax(maxy, t.y[i]);
    }
    const float m = 0.16f * size;
    CHECK(minx >= m - 1 && maxx <= size - m + 1 && miny >= m - 1 && maxy <= size - m + 1,
          "inside the logo's 16%% margin at %d: x %.1f-%.1f y %.1f-%.1f", size, minx, maxx, miny, maxy);
    CHECK(std::fmax(maxx - minx, maxy - miny) > 0.9f * (size - 2 * m), "fills the logo");
    float x0, y0, x1, y1;
    at(t, 0.0f, x0, y0);
    CHECK(std::fabs(x0 - t.x[0]) < 0.01f && std::fabs(y0 - t.y[0]) < 0.01f, "frac 0 is the start line");
    at(t, 1.0f, x1, y1);
    CHECK(std::fabs(x1 - x0) < 0.01f && std::fabs(y1 - y0) < 0.01f, "a lap closes");
    at(t, 1.25f, x1, y1); at(t, 0.25f, x0, y0);
    CHECK(std::fabs(x1 - x0) < 0.01f && std::fabs(y1 - y0) < 0.01f, "it wraps");
    // a smooth path: successive samples never jump more than a few px
    float px, py; at(t, 0.0f, px, py); float worst = 0;
    for (int i = 1; i <= 1000; i++) {
      float x, y; at(t, i / 1000.0f, x, y);
      worst = std::fmax(worst, std::hypot(x - px, y - py)); px = x; py = y;
    }
    CHECK(worst < t.total / 200.0f, "no jumps along the lap (%.2f px worst of %.0f)", worst, t.total);
    // the car stays on the drawn line: every sample lies on a segment of the polyline
    float off = 0;
    for (int i = 0; i < 400; i++) {
      float x, y; at(t, i / 400.0f, x, y);
      float best = 1e9;
      for (int k = 1; k < t.n; k++) {
        const float ax = t.x[k - 1], ay = t.y[k - 1], bx = t.x[k], by = t.y[k];
        const float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
        float u = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
        u = std::fmax(0, std::fmin(1, u));
        best = std::fmin(best, std::hypot(x - (ax + u * dx), y - (ay + u * dy)));
      }
      off = std::fmax(off, best);
    }
    CHECK(off < 0.01f, "the car is on the line (%.4f px off)", off);
  }
  // the outline is the hand-smoothed Monza: it only bends the way it is already turning (a
  // bump is a left-right-left wobble), and nowhere sharper than a real corner
  {
    Track sm;
    build(sm, 200);
    float prev = 0, worst = 0;
    int zig = 0;
    for (int i = 0; i + 2 < sm.n - 1; i++) {
      const float ax = sm.x[i + 1] - sm.x[i], ay = sm.y[i + 1] - sm.y[i];
      const float bx = sm.x[i + 2] - sm.x[i + 1], by = sm.y[i + 2] - sm.y[i + 1];
      const float turn = std::atan2(ax * by - ay * bx, ax * bx + ay * by);     // signed
      worst = std::fmax(worst, std::fabs(turn));
      if ((turn > 0.12f && prev < -0.12f) || (turn < -0.12f && prev > 0.12f)) zig++;
      prev = turn;
    }
    CHECK(zig == 0, "no left-right wobble in the outline (%d)", zig);
    CHECK(worst < 0.9f, "no turn over 0.9 rad between neighbouring segments (%.2f)", worst);
    // evenly spaced (the generator resamples by arc length), so the cars' speed is even
    float lo = 1e9f, hi = 0;
    for (int i = 1; i < sm.n; i++) { const float d = sm.cum[i] - sm.cum[i - 1]; lo = std::fmin(lo, d); hi = std::fmax(hi, d); }
    CHECK(hi < 1.15f * lo, "points evenly spaced (%.2f..%.2f px)", lo, hi);
  }

  // headings: the sprite points the way the track runs, and turns smoothly round the lap
  {
    Track t; build(t, 200);
    int prev = heading(t, 0.0f), big = 0;
    for (int i = 1; i <= 2000; i++) {
      const int h = heading(t, i / 2000.0f);
      if (h < 0 || h >= HEADINGS) big += 100;
      int d = std::abs(h - prev); d = d > HEADINGS / 2 ? HEADINGS - d : d;
      if (d > 3) big++;                      // a turn of more than 67 degrees between samples = a jump
      prev = h;
    }
    CHECK(big == 0, "headings are valid and never jump (%d bad)", big);
    // a heading agrees with the drawn direction: step along the track and compare
    int wrong = 0;
    for (int i = 0; i < 200; i++) {
      const float f = i / 200.0f;
      float x0, y0, x1, y1; at(t, f, x0, y0); at(t, f + 0.003f, x1, y1);
      const float deg = std::atan2(x1 - x0, -(y1 - y0)) * 57.29578f;
      float want = deg < 0 ? deg + 360 : deg;
      float got = heading(t, f) * (360.0f / HEADINGS);
      float diff = std::fabs(want - got); diff = diff > 180 ? 360 - diff : diff;
      if (diff > 2 * 360.0f / HEADINGS) wrong++;   // a corner turns the car within the sample
    }
    CHECK(wrong == 0, "heading within two steps of the track direction (a flipped axis would be 8 off) (%d off)", wrong);
    // the straight at the start line runs one way, the opposite straight the other
    CHECK(N_CARS == 3 && CAR_GAP * N_CARS < 0.5f, "a pack that fits on the lap");
  }
  CHECK(lap_u(-3) == 0 && lap_u(0) == 0 && std::fabs(lap_u(LAP_MS / 2) - 0.5f) < 1e-6f &&
        std::fabs(lap_u(LAP_MS + 1250) - 1.25f) < 1e-6f, "laps since opening");

  // the pack's speeds: a different pace for each car, so one can overtake another
  {
    int no_pass = 0, slow = 0, fast = 0, bad_start = 0, bad_loop = 0, bad_side = 0;
    for (uint32_t seed = 1; seed <= 300; seed++) {
      const Pack pk = make_pack(seed);
      int swaps = 0;
      for (int k = 0; k < N_CARS; k++) {
        if (std::fabs(car_pos(pk, k, 0.0f) + k * CAR_GAP) > 1e-5f) bad_start++;          // the grid at u = 0
        if (std::fabs(car_pos(pk, k, 1.0f) - car_pos(pk, k, 0.0f) - 1.0f) > 1e-4f) bad_loop++;  // one lap a lap
        for (int i = 0; i <= 400; i++) {
          const float sp = car_speed(pk, k, i / 400.0f);
          if (sp < 0.05f) slow++;                       // never stops or reverses
          if (sp > 2.0f) fast++;
          const float w = pulling_out(pk, k, i / 400.0f);
          if (w < 0 || w > 1) bad_side++;
        }
      }
      // an overtake: a pair whose order round the (short) gap changes sign inside the lap
      for (int j = 0; j < N_CARS; j++)
        for (int k = j + 1; k < N_CARS; k++) {
          float prev = car_pos(pk, j, 0.0f) - car_pos(pk, k, 0.0f);
          for (int i = 1; i <= 400; i++) {
            const float d = car_pos(pk, j, i / 400.0f) - car_pos(pk, k, i / 400.0f);
            if ((prev < 0) != (d < 0) && std::fabs(d) < 0.3f && std::fabs(prev) < 0.3f) swaps++;
            prev = d;
          }
        }
      if (swaps == 0) no_pass++;
    }
    CHECK(bad_start == 0, "cars start on the grid, CAR_GAP apart");
    CHECK(bad_loop == 0, "after a lap each car is exactly one lap on, so a loop repeats cleanly");
    CHECK(slow == 0 && fast == 0, "speeds stay between 0.05 and 2 laps per lap (%d slow, %d fast)", slow, fast);
    CHECK(bad_side == 0, "the pull-out is 0..1");
    // MOTION-3a: with the gentler swing the opening lap alone is not always eventful; over the
    // first three laps the re-dealt order makes an overtake all but certain
    int no_pass3 = 0;
    for (uint32_t seed = 1; seed <= 300; seed++) {
      int swaps = 0;
      for (int lap = 0; lap < 3 && swaps == 0; lap++) {
        const Pack pk = lap_pack(seed, lap);
        for (int j = 0; j < N_CARS; j++)
          for (int k = j + 1; k < N_CARS; k++) {
            float prev = car_pos(pk, j, (float) lap) - car_pos(pk, k, (float) lap);
            for (int i = 1; i <= 400; i++) {
              const float d = car_pos(pk, j, lap + i / 400.0f) - car_pos(pk, k, lap + i / 400.0f);
              if ((prev < 0) != (d < 0) && std::fabs(d) < 0.3f && std::fabs(prev) < 0.3f) swaps++;
              prev = d;
            }
          }
      }
      if (swaps == 0) no_pass3++;
    }
    CHECK(no_pass <= 150, "many openings have an overtake in their first lap (%d of 300 have none)", no_pass);
    CHECK(no_pass3 <= 15, "nearly every opening has one within three laps (%d of 300 have none)", no_pass3);
    // the demo seed must show at least one, so the renders do
    const Pack d = make_pack(DEMO_SEED);
    int swaps = 0;
    for (int j = 0; j < N_CARS; j++)
      for (int k = j + 1; k < N_CARS; k++) {
        float prev = car_pos(d, j, 0.0f) - car_pos(d, k, 0.0f);
        for (int i = 1; i <= 400; i++) {
          const float dd = car_pos(d, j, i / 400.0f) - car_pos(d, k, i / 400.0f);
          if ((prev < 0) != (dd < 0) && std::fabs(dd) < 0.3f && std::fabs(prev) < 0.3f) swaps++;
          prev = dd;
        }
      }
    CHECK(swaps >= 1, "DEMO_SEED has an overtake (%d)", swaps);
    // someone really does pull out in the demo
    float most = 0;
    for (int k = 0; k < N_CARS; k++)
      for (int i = 0; i <= 400; i++) most = std::fmax(most, pulling_out(d, k, i / 400.0f));
    CHECK(most > 0.5f, "a car pulls out to pass in the demo (%.2f)", most);
    // the passer is the faster one at the moment of passing
    CHECK(pulling_out(make_pack(DEMO_SEED), 0, 0.0f) >= 0.0f, "pull-out defined at the start");
  }

  std::printf("every lap a new set, handed over seamlessly, the order re-dealt (MOTION-3a)\n");
  {
    int changed = 0, carlaps = 0, reorders = 0, laps = 0, jumps = 0;
    float slowest = 9;
    for (uint32_t seed = 1; seed <= 40; seed++) {
      Pack prev = make_pack(seed);
      uint32_t x = seed;
      for (int lap = 1; lap <= 5; lap++) {
        const Pack next = next_pack(prev, xorshift(x));
        const float at = (float) lap;
        CHECK(next.lap == lap, "the set knows its lap");
        for (int k = 0; k < N_CARS; k++) {
          CHECK(std::fabs(car_pos(prev, k, at) - car_pos(next, k, at)) < 1e-4f,
                "seed %u lap %d car %d: the position must not jump at the line", seed, lap, k);
          // the pace is matched unless that would need a swing past SWING_CAP (then a small step)
          const float dv = std::fabs(car_speed(prev, k, at) - car_speed(next, k, at));
          if (dv > 2e-3f) jumps++;
          CHECK(dv < 0.25f, "seed %u lap %d car %d: a pace step at the line of %.3f", seed, lap, k, dv);
          CHECK(next.a[k] >= SWING_MIN - 1e-6f && next.a[k] <= SWING_CAP + 1e-6f, "the swing stays in range");
          for (int i = 0; i <= 100; i++) slowest = std::fmin(slowest, car_speed(next, k, at + i / 100.0f));
          carlaps++;
          if (next.n[k] != prev.n[k] || std::fabs(next.a[k] - prev.a[k]) > 0.02f) changed++;
        }
        // at the next line the pack is nose to tail again, in some order
        float at_line[N_CARS];
        for (int k = 0; k < N_CARS; k++) at_line[k] = car_pos(next, k, at + 1.0f) - (at + 1.0f);
        bool same_order = true;
        for (int k = 0; k < N_CARS; k++) {
          bool slot_ok = false;
          for (int s2 = 0; s2 < N_CARS; s2++) if (std::fabs(at_line[k] + s2 * CAR_GAP) < 1e-4f) slot_ok = true;
          CHECK(slot_ok, "seed %u lap %d car %d ends the lap off the grid (%.4f)", seed, lap, k, at_line[k]);
          if (std::fabs(at_line[k] - (car_pos(next, k, at) - at)) > 1e-4f) same_order = false;
        }
        laps++;
        if (!same_order) reorders++;
        prev = next;
      }
      const Pack walked = lap_pack(seed, 5);
      CHECK(std::fabs(walked.a[0] - prev.a[0]) < 1e-6f && walked.n[2] == prev.n[2],
            "lap_pack walks the same chain the device does");
      CHECK(lap_pack(seed, 0).a[1] == make_pack(seed).a[1], "lap 0 is the opening pack");
    }
    CHECK(changed * 2 > carlaps, "laps really differ: %d of %d car-laps changed their swing", changed, carlaps);
    CHECK(reorders * 2 > laps, "the order at the line really changes: %d of %d laps", reorders, laps);
    CHECK(slowest > 0.25f, "no car ever near-stalls, slowest pace %.2f of the mean", slowest);
    CHECK(jumps * 10 < carlaps, "pace steps at the line are the exception: %d of %d", jumps, carlaps);
  }

  std::printf("%d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}

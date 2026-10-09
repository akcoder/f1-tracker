#pragma once
// The Sun's elevation from a latitude, a longitude and the clock - the only
// thing the observer position is for (decision 7), and the input to auto-dim.
//
// Ported from sky-tracker 4.6.27 (sky_math.h: astro::sun, gmst_deg, horizontal),
// which uses the low-precision formulae of the Astronomical Almanac (sections C
// and D): good to about 0.01 deg, with the equation of time included. The first
// version of this project used a cheaper declination-only formula with no
// equation of time, which is up to ~4 deg out around the equinoxes - harmless
// for a 13 deg ramp, but there is no reason to be less accurate than the
// sibling that already got it right.
//
// LVGL-free so tests/ can pin it against known solar geometry.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace f1 {
namespace sun {

inline double rad(double d) { return d * M_PI / 180.0; }
inline double wrap360(double d) {
  d = std::fmod(d, 360.0);
  return d < 0 ? d + 360.0 : d;
}
inline double jd(double unix_s) { return unix_s / 86400.0 + 2440587.5; }

// Greenwich mean sidereal time, degrees.
inline double gmst_deg(double jdv) {
  return wrap360(280.46061837 + 360.98564736629 * (jdv - 2451545.0));
}

struct Equ { double ra, dec; };      // radians

inline Equ position(double jdv) {
  const double n = jdv - 2451545.0;
  const double L = wrap360(280.460 + 0.9856474 * n);
  const double g = rad(wrap360(357.528 + 0.9856003 * n));
  const double lam = rad(L + 1.915 * std::sin(g) + 0.020 * std::sin(2 * g));
  const double eps = rad(23.439 - 0.0000004 * n);
  return {std::atan2(std::cos(eps) * std::sin(lam), std::cos(lam)),
          std::asin(std::sin(eps) * std::sin(lam))};
}

// Elevation of the Sun's centre above the horizon, degrees, with atmospheric
// refraction applied above -1 deg (Saemundsson), as sky-tracker reports it.
// East longitude is positive.
inline float elevation(double unix_s, double lat_deg, double lon_deg) {
  const double j = jd(unix_s);
  const Equ e = position(j);
  const double lst = rad(wrap360(gmst_deg(j) + lon_deg));
  const double la = rad(lat_deg), H = lst - e.ra;
  const double s = std::sin(la) * std::sin(e.dec) + std::cos(la) * std::cos(e.dec) * std::cos(H);
  double el = std::asin(std::max(-1.0, std::min(1.0, s))) * 180.0 / M_PI;
  if (el > -1.0) el += 1.02 / std::tan(rad(el + 10.3 / (el + 5.11))) / 60.0;
  return (float) el;
}

}  // namespace sun
}  // namespace f1

#pragma once
// Firmware updates, the part that can be tested without a screen (UI-68, ported
// from sky-tracker 4.6.27's sky_update.h): version comparison, which answers
// count as "an update", the release notes as the panel can show them, and how
// the prompt moves between its states. LVGL-free; f1_update.h is the card.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace f1 {
namespace upd {

// What ESPHome's update entity says (update::UpdateState).
enum EntState : int { E_UNKNOWN = 0, E_NO_UPDATE = 1, E_AVAILABLE = 2, E_INSTALLING = 3 };

enum Mode : uint8_t {
  M_NONE = 0, M_CHECKING, M_LATEST, M_AVAILABLE, M_INSTALLING, M_FAILED, M_INSTALL_FAILED
};

// "0.10.0" is newer than "0.9.0": digit runs are compared as numbers, so a
// string compare (which puts "10" before "9") would never offer 0.10.
inline int vcmp(const char *a, const char *b) {
  while (*a && *b) {
    if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9') {
      const unsigned long x = std::strtoul(a, (char **) &a, 10);
      const unsigned long y = std::strtoul(b, (char **) &b, 10);
      if (x != y) return x < y ? -1 : 1;
    } else {
      if (*a != *b) return (unsigned char) *a < (unsigned char) *b ? -1 : 1;
      a++, b++;
    }
  }
  return *a ? 1 : *b ? -1 : 0;
}

// ESPHome reports ANY difference between the manifest and the running firmware
// as "update available" - an older release included. sky-tracker 4.6.5 offered
// the GitHub 4.6.3, it was accepted, and a working build was replaced by an older
// one. Only a NEWER version counts as an update here.
inline int newer_only(int state, const char *latest, const char *current) {
  if (state == E_AVAILABLE && vcmp(latest ? latest : "", current ? current : "") <= 0)
    return E_NO_UPDATE;
  return state;
}

// The release notes as a 400 px box can show them: Markdown's emphasis marks,
// code ticks and heading hashes dropped, runs of blank lines squeezed, typographic
// punctuation folded to what the fonts have, at most ~1.5 KB.
inline std::string fold_text(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size();) {
    const unsigned char c = (unsigned char) in[i];
    if (c < 0x80) { out += (char) c; i++; continue; }
    // UTF-8: the Latin the fonts carry (U+0080..U+07FF, two bytes) passes through;
    // the common typographic marks become ASCII; anything else becomes '?'.
    if ((c & 0xE0) == 0xC0 && i + 1 < in.size()) {
      if (c == 0xC2 && (unsigned char) in[i + 1] == 0xA0) out += ' ';          // no-break space
      else out.append(in, i, 2);
      i += 2; continue;
    }
    if ((c & 0xF0) == 0xE0 && i + 2 < in.size()) {
      const unsigned cp = ((c & 0x0Fu) << 12) | (((unsigned char) in[i + 1] & 0x3Fu) << 6) |
                          ((unsigned char) in[i + 2] & 0x3Fu);
      switch (cp) {
        case 0x2018: case 0x2019: out += '\''; break;
        case 0x201C: case 0x201D: out += '"'; break;
        case 0x2013: case 0x2014: case 0x2212: out += '-'; break;
        case 0x2022: out += '-'; break;
        case 0x2026: out += "..."; break;
        default: out += '?'; break;
      }
      i += 3; continue;
    }
    out += '?';
    i += ((c & 0xF8) == 0xF0) ? 4 : 1;
  }
  return out;
}

inline std::string notes_text(const std::string &md) {
  std::string out;
  out.reserve(md.size() < 1600 ? md.size() : 1600);
  bool line_start = true;
  int blank = 0;
  for (size_t i = 0; i < md.size() && out.size() < 1500; i++) {
    const char c = md[i];
    if (c == '\r' || c == '*' || c == '`') continue;
    if (line_start && c == '#') {
      while (i + 1 < md.size() && (md[i + 1] == '#' || md[i + 1] == ' ')) i++;
      continue;
    }
    if (c == '\n') {
      if (++blank > 2) continue;
      out += c;
      line_start = true;
      continue;
    }
    blank = 0;
    line_start = false;
    out += c;
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
  return fold_text(out);
}

// How the prompt follows the entity, every 500 ms. `elapsed_ms` is time since
// the current mode began. Returns the mode to be in (the same one: no change).
inline constexpr uint32_t CHECK_SETTLE_MS = 1500;     // "up to date" is not trusted sooner
inline constexpr uint32_t CHECK_GIVE_UP_MS = 25000;
inline constexpr uint32_t INSTALL_GIVE_UP_MS = 120000;

inline Mode next_mode(Mode m, int s, uint32_t elapsed_ms) {
  switch (m) {
    case M_CHECKING:
      if (s == E_AVAILABLE) return M_AVAILABLE;
      if (s == E_NO_UPDATE && elapsed_ms > CHECK_SETTLE_MS) return M_LATEST;
      if (elapsed_ms > CHECK_GIVE_UP_MS) return s == E_NO_UPDATE ? M_LATEST : M_FAILED;
      return m;
    case M_INSTALLING:
      // back to "available" long after it began: the install did not happen
      if (s == E_AVAILABLE && elapsed_ms > INSTALL_GIVE_UP_MS) return M_AVAILABLE;
      return m;
    default:
      return m;
  }
}

// UI-68a: the hourly check never opens anything by itself. A newer version being
// available shows an icon, and only on a page that has the gear beside it.
inline bool icon_wanted(int s, Mode m, bool page_allows) {
  return s == E_AVAILABLE && m != M_INSTALLING && page_allows;
}

}  // namespace upd
}  // namespace f1

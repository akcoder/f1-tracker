#pragma once
// A minimal selective JSON scanner.
//
// Section 4.2 asks for a streaming/selective parser rather than a DOM: the
// 48 KB sessions response carries ~20 fields per row and we use eight. This
// walks the text and pulls named values out of the current object without
// allocating anything, which also means it builds on the host with no
// dependency, so tests/ can run it against the committed fixtures.
//
// It is deliberately NOT a general JSON parser. It assumes the well-formed,
// known-shaped responses in reference/samples/ and is used only on those.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace f1 {
namespace json {

// Skip a string starting at the opening quote; returns the index just past the
// closing quote. Handles backslash escapes so a quote inside a value does not
// end it early.
inline size_t skip_string(const char *s, size_t i, size_t n) {
  if (i >= n || s[i] != '"') return i;
  for (i++; i < n; i++) {
    if (s[i] == '\\') { i++; continue; }
    if (s[i] == '"') return i + 1;
  }
  return n;
}

// Index just past the value starting at i (object, array, string, or scalar).
inline size_t skip_value(const char *s, size_t i, size_t n) {
  while (i < n && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++;
  if (i >= n) return n;
  if (s[i] == '"') return skip_string(s, i, n);
  if (s[i] == '{' || s[i] == '[') {
    const char open = s[i], close = (open == '{') ? '}' : ']';
    int depth = 0;
    for (; i < n; i++) {
      if (s[i] == '"') { i = skip_string(s, i, n) - 1; continue; }
      if (s[i] == open) depth++;
      else if (s[i] == close) { depth--; if (depth == 0) return i + 1; }
    }
    return n;
  }
  while (i < n && s[i] != ',' && s[i] != '}' && s[i] != ']') i++;
  return i;
}

// Find "key" at the TOP level of the object beginning at `obj` (which points at
// its '{'). Nested objects are skipped, so a key of the same name deeper in the
// tree cannot be picked up by accident. Returns the index of the value, or
// npos.
inline constexpr size_t NPOS = (size_t) -1;

inline size_t find_key(const char *s, size_t obj, size_t n, const char *key) {
  if (obj >= n || s[obj] != '{') return NPOS;
  const size_t klen = std::strlen(key);
  size_t i = obj + 1;
  while (i < n) {
    while (i < n && (s[i] == ' ' || s[i] == ',' || s[i] == '\n' || s[i] == '\r' ||
                     s[i] == '\t')) i++;
    if (i >= n || s[i] == '}') return NPOS;
    if (s[i] != '"') return NPOS;
    const size_t kstart = i + 1;
    const size_t kend = skip_string(s, i, n);
    const size_t klen_found = kend - kstart - 1;
    i = kend;
    while (i < n && (s[i] == ' ' || s[i] == ':')) i++;
    if (klen_found == klen && std::strncmp(s + kstart, key, klen) == 0) return i;
    i = skip_value(s, i, n);
  }
  return NPOS;
}

// Copy a string value into out. Returns false when the key is absent or null,
// which is the common case this project must tolerate (section 3.4): a missing
// field is NOT an error and must not become an empty-but-present value by
// accident.
inline bool get_str(const char *s, size_t obj, size_t n, const char *key,
                    char *out, size_t out_len) {
  if (out_len) out[0] = '\0';
  const size_t v = find_key(s, obj, n, key);
  if (v == NPOS || v >= n) return false;
  if (s[v] != '"') return false;            // null, number, object: not a string
  size_t i = v + 1, o = 0;
  while (i < n && s[i] != '"' && o + 1 < out_len) {
    if (s[i] == '\\' && i + 1 < n) {
      i++;
      switch (s[i]) {
        case 'n': out[o++] = '\n'; break;
        case 't': out[o++] = '\t'; break;
        case 'u': i += 4; break;             // \uXXXX: not needed by our fields
        default:  out[o++] = s[i]; break;
      }
      i++;
    } else {
      out[o++] = s[i++];
    }
  }
  out[o] = '\0';
  return true;
}

inline bool get_double(const char *s, size_t obj, size_t n, const char *key, double &out) {
  const size_t v = find_key(s, obj, n, key);
  if (v == NPOS || v >= n) return false;
  if (s[v] == 'n') return false;                       // null
  const char *p = s + v;
  if (*p == '"') p++;                                  // Jolpica quotes its numbers
  char *end = nullptr;
  const double d = std::strtod(p, &end);
  if (end == p) return false;
  out = d;
  return true;
}

inline bool get_int(const char *s, size_t obj, size_t n, const char *key, long &out) {
  double d;
  if (!get_double(s, obj, n, key, d)) return false;
  out = (long) d;
  return true;
}

// The object at index i of the array whose '[' is at `arr`, or NPOS.
inline size_t array_at(const char *s, size_t arr, size_t n, int index) {
  if (arr >= n || s[arr] != '[') return NPOS;
  size_t i = arr + 1;
  int k = 0;
  while (i < n) {
    while (i < n && (s[i] == ' ' || s[i] == ',' || s[i] == '\n' || s[i] == '\r' ||
                     s[i] == '\t')) i++;
    if (i >= n || s[i] == ']') return NPOS;
    if (k == index) return i;
    i = skip_value(s, i, n);
    k++;
  }
  return NPOS;
}

inline int array_len(const char *s, size_t arr, size_t n) {
  if (arr >= n || s[arr] != '[') return 0;
  size_t i = arr + 1;
  int k = 0;
  while (i < n) {
    while (i < n && (s[i] == ' ' || s[i] == ',' || s[i] == '\n' || s[i] == '\r' ||
                     s[i] == '\t')) i++;
    if (i >= n || s[i] == ']') return k;
    i = skip_value(s, i, n);
    k++;
  }
  return k;
}

// Walk a dotted path of object keys: "MRData.RaceTable.Races".
inline size_t path(const char *s, size_t n, const char *dotted) {
  size_t cur = 0;
  while (cur < n && s[cur] != '{') cur++;
  char key[64];
  const char *p = dotted;
  while (*p) {
    const char *dot = std::strchr(p, '.');
    const size_t len = dot ? (size_t) (dot - p) : std::strlen(p);
    if (len >= sizeof(key)) return NPOS;
    std::memcpy(key, p, len);
    key[len] = '\0';
    cur = find_key(s, cur, n, key);
    if (cur == NPOS) return NPOS;
    p = dot ? dot + 1 : p + len;
  }
  return cur;
}

}  // namespace json
}  // namespace f1

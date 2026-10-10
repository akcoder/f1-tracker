#pragma once
// NET-10: the web page's tab icon and header badge.
//
// from sky-tracker 4.5.34 (its sky_web.h), adapted. Recorded per decision 45 so
// a later divergence can be diffed rather than guessed at.
//
// ESPHome's bundled web UI (web_server version 3, local: true) ships an empty
// icon - <link rel=icon href=data:image /> - and has no option to change it;
// js_include is not loaded by the bundled page. So "/" is answered here first:
// ESPHome's own gzipped page (INDEX_GZ, the same bytes web_server would send)
// is inflated once into PSRAM with the ROM's miniz, the icon link and the
// header logo are swapped for ours, and the result is served uncompressed
// (~78 kB, LAN only). The page itself is otherwise untouched, so an ESPHome
// update brings its new UI along.
//
// Anything that goes wrong - no PSRAM, an unexpected page - leaves ESPHome's
// own handler to serve its page as before. A missing tab icon is not worth a
// failed boot.
#include "esphome/core/defines.h"

#if defined(USE_ESP32) && defined(USE_WEBSERVER) && defined(USE_WEBSERVER_LOCAL) && \
    defined(USE_WEBSERVER_GZIP) && USE_WEBSERVER_VERSION == 3

#include <cstring>
#include <string>
#include <string_view>

#include "esp_heap_caps.h"
#include "esphome/components/web_server/server_index_v3.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/log.h"
#include "miniz.h"
#ifdef USE_CAPTIVE_PORTAL
#include "esphome/components/captive_portal/captive_portal.h"
#endif

#include "f1_logo.h"

namespace f1 {
namespace web {

static const char *const TAG = "f1_web";

inline char *page = nullptr;
inline size_t page_len = 0;

#define PROJECT_URL "https://github.com/akcoder/f1-tracker"

// The header logo already sits inside the page's own link (to ESPHome's web API docs);
// point that at the project page instead, as sky-tracker does at build time (its
// __init__.py patches the same anchor). The SVG itself is swapped separately, above.
// A page worded differently is left as it is.
inline void link_logo() {
  static const char OLD[] = "href=\"https://esphome.io/web-api\" id=\"logo\"";
  static const char NEW[] = "href=\"" PROJECT_URL "\" id=\"logo\" target=\"_blank\" rel=\"noopener\"";
  const std::string_view all(page, page_len);
  const size_t at = all.find(OLD);
  if (at == std::string_view::npos) {
    ESP_LOGW(TAG, "logo link not found; it stays as ESPHome has it");
    return;
  }
  const size_t olen = sizeof(OLD) - 1, nlen = sizeof(NEW) - 1, len = page_len - olen + nlen;
  auto *np = (char *) heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (np == nullptr) return;
  std::memcpy(np, page, at);
  std::memcpy(np + at, NEW, nlen);
  std::memcpy(np + at + nlen, page + at + olen, page_len - at - olen);
  heap_caps_free(page);
  page = np;
  page_len = len;
}

// Inflate ESPHome's page and put our mark in. Returns false - and serves
// nothing of its own - on any doubt.
inline bool build() {
  const uint8_t *gz = esphome::web_server::INDEX_GZ;
  const size_t gz_len = sizeof(esphome::web_server::INDEX_GZ);
  // gzip: 10-byte header with no optional fields (FLG = 0), raw deflate, 8-byte trailer
  if (gz_len < 18 || gz[0] != 0x1f || gz[1] != 0x8b || gz[2] != 8 || gz[3] != 0) return false;
  const uint32_t raw_len = gz[gz_len - 4] | (gz[gz_len - 3] << 8) |
                           (gz[gz_len - 2] << 16) | ((uint32_t) gz[gz_len - 1] << 24);
  if (raw_len == 0 || raw_len > 512 * 1024) return false;

  auto *raw = (uint8_t *) heap_caps_malloc(raw_len, MALLOC_CAP_SPIRAM);
  // ~11 kB: deliberately not on the stack
  auto *inf = (tinfl_decompressor *) heap_caps_malloc(sizeof(tinfl_decompressor),
                                                      MALLOC_CAP_SPIRAM);
  bool ok = raw != nullptr && inf != nullptr;
  if (ok) {
    tinfl_init(inf);
    size_t in_len = gz_len - 18, out_len = raw_len;
    ok = tinfl_decompress(inf, gz + 10, &in_len, raw, raw, &out_len,
                          TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) == TINFL_STATUS_DONE &&
         out_len == raw_len;
  }
  heap_caps_free(inf);
  if (!ok) {
    heap_caps_free(raw);
    ESP_LOGW(TAG, "could not unpack the web page; tab icon unchanged");
    return false;
  }

  const std::string_view html((const char *) raw, raw_len);
  const std::string icon = std::string("<link rel=icon type=\"image/svg+xml\" href=\"") +
                           "data:image/svg+xml," + logo::FAVICON_SVG + "\">";

  static const char OLD[] = "<link rel=icon href=data:image />";
  size_t at = html.find(OLD), cut = sizeof(OLD) - 1;
  if (at == std::string_view::npos) {   // a newer page: add it right after <head>
    at = html.find("<head>");
    if (at == std::string_view::npos) {
      heap_caps_free(raw);
      ESP_LOGW(TAG, "web page not recognised; tab icon unchanged");
      return false;
    }
    at += 6;
    cut = 0;
  }

  // The header badge: esp-logo renders a fixed SVG string, which we replace.
  static const char LOGO_OLD[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?> <svg id=\"Layer_2\"";
  size_t lat = html.find(LOGO_OLD, at + cut), lcut = 0, ladd = 0;
  if (lat != std::string_view::npos) {
    const size_t lend = html.find("</svg>`", lat);
    if (lend != std::string_view::npos) {
      lcut = lend + 6 - lat;
      ladd = (size_t) logo::HEADER_SVG_LEN;
    }
  }
  if (ladd == 0) {
    lat = raw_len;
    ESP_LOGW(TAG, "header logo not found; left as is");
  }

  const size_t add = icon.size(), len = raw_len - cut + add - lcut + ladd;
  page = (char *) heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
  if (page != nullptr) {
    char *o = page;
    auto put = [&](const void *src, size_t n) { std::memcpy(o, src, n); o += n; };
    put(raw, at);
    put(icon.data(), add);
    put(raw + at + cut, lat - at - cut);
    if (ladd) {
      put(logo::HEADER_SVG, ladd);
      put(raw + lat + lcut, raw_len - lat - lcut);
    }
    page_len = len;
    // Stop the app replacing the icon on first render: its selector
    // link[rel~='icon'] is renamed to one that matches nothing, same length.
    static const char SEL[] = "link[rel~='icon']";
    int n = 0;
    const std::string_view all(page, len);
    for (size_t a2 = all.find(SEL); a2 != std::string_view::npos;
         a2 = all.find(SEL, a2 + 1), n++)
      std::memcpy(page + a2 + 11, "none", 4);
    if (n == 0) ESP_LOGW(TAG, "app icon selector not found; the app may replace the icon");
    link_logo();
  }
  heap_caps_free(raw);
  return page != nullptr;
}

class IndexHandler : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    if (page == nullptr || request->method() != HTTP_GET) return false;
#ifdef USE_CAPTIVE_PORTAL
    // NET-2: the Wi-Fi setup page owns "/" while the fallback AP is up.
    if (esphome::captive_portal::global_captive_portal != nullptr &&
        esphome::captive_portal::global_captive_portal->is_active())
      return false;
#endif
    char buf[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->url_to(buf) == "/";
  }
  void handleRequest(AsyncWebServerRequest *request) override {
    request->send(request->beginResponse(200, "text/html", (const uint8_t *) page, page_len));
  }
};

// Call before web_server's own setup (priority 249) so this handler is asked first.
inline void install() {
  auto *base = esphome::web_server_base::global_web_server_base;
  if (base == nullptr || page != nullptr || !build()) return;
  base->add_handler(new IndexHandler());  // NOLINT: lives for the program
  ESP_LOGI(TAG, "web page icon and badge installed (%u bytes)", (unsigned) page_len);
}

}  // namespace web
}  // namespace f1

#else
namespace f1 {
namespace web {
inline void install() {}
}  // namespace web
}  // namespace f1
#endif

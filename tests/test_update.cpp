// Firmware updates (UI-68): version comparison, what counts as an update, the
// release notes, and the prompt's state machine. The failure these guard against
// is a real one from sky-tracker 4.6.5: ESPHome calls ANY version difference an
// update, so an older release was offered, accepted and installed over a newer one.
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../f1-tracker/f1_updlogic.h"

static int checks = 0, failures = 0;
static void okf(bool c, const char *fmt, ...) {
  checks++;
  if (!c) {
    failures++;
    va_list ap; va_start(ap, fmt);
    std::printf("  FAIL  "); std::vprintf(fmt, ap); std::printf("\n"); va_end(ap);
  }
}

using namespace f1::upd;

static void test_versions() {
  std::printf("version comparison\n");
  okf(vcmp("0.10.0", "0.9.0") > 0, "0.10.0 is newer than 0.9.0 (not a string compare)");
  okf(vcmp("0.9.0", "0.10.0") < 0, "...and the reverse");
  okf(vcmp("0.1.0", "0.1.0") == 0, "equal");
  okf(vcmp("0.2.0", "0.1.9") > 0 && vcmp("1.0.0", "0.99.99") > 0, "ordinary ordering");
  okf(vcmp("1.0", "1.0.1") < 0, "a longer version with the same prefix is newer");
  okf(vcmp("1.0.1", "1.0") > 0, "...either way round");
  okf(vcmp("0.1.0", "0.1.0-beta") < 0, "a suffix sorts after the bare version (release versions carry none)");
  okf(vcmp("", "") == 0 && vcmp("1", "") > 0 && vcmp("", "1") < 0, "empty strings");
  okf(vcmp("007", "7") == 0, "leading zeros are numbers");
}

static void test_newer_only() {
  std::printf("only a NEWER version is an update\n");
  okf(newer_only(E_AVAILABLE, "0.2.0", "0.1.0") == E_AVAILABLE, "newer: offered");
  okf(newer_only(E_AVAILABLE, "0.1.0", "0.2.0") == E_NO_UPDATE,
      "OLDER: not offered, though ESPHome says available (the sky-tracker 4.6.5 bug)");
  okf(newer_only(E_AVAILABLE, "0.1.0", "0.1.0") == E_NO_UPDATE, "the same version: not offered");
  okf(newer_only(E_AVAILABLE, "0.10.0", "0.9.0") == E_AVAILABLE, "0.10.0 over 0.9.0: offered");
  okf(newer_only(E_NO_UPDATE, "9.9.9", "0.1.0") == E_NO_UPDATE, "only the AVAILABLE state is reinterpreted");
  okf(newer_only(E_UNKNOWN, "9.9.9", "0.1.0") == E_UNKNOWN, "unknown stays unknown");
  okf(newer_only(E_INSTALLING, "0.1.0", "0.2.0") == E_INSTALLING, "installing stays installing");
  okf(newer_only(E_AVAILABLE, nullptr, nullptr) == E_NO_UPDATE, "null versions are not an update");
}

static void test_notes() {
  std::printf("release notes\n");
  const std::string md = "## What's new\n\n**Boot page** and `About` screen\n\n\n\n\n- one\n- two\n";
  const std::string t = notes_text(md);
  okf(t.find('*') == std::string::npos && t.find('`') == std::string::npos && t.find('#') == std::string::npos,
      "emphasis, ticks and heading marks dropped: [%s]", t.c_str());
  okf(t.find("\n\n\n\n") == std::string::npos, "blank-line runs squeezed");
  okf(t.find("Boot page and About screen") != std::string::npos, "the words survive");
  okf(!t.empty() && t.back() != '\n' && t.back() != ' ', "trailing whitespace trimmed");
  okf(t.find("What's new") != std::string::npos, "the heading text is kept");

  std::string big(5000, 'x');
  okf(notes_text(big).size() <= 1500, "capped at ~1.5 KB (%zu)", notes_text(big).size());
  okf(notes_text("").empty(), "no notes, no text");

  // A GitHub release is full of typographic characters the fonts may not carry.
  okf(notes_text("it\xE2\x80\x99s \xE2\x80\x9Cgood\xE2\x80\x9D \xE2\x80\x94 ok") == "it's \"good\" - ok",
      "curly quotes and the em dash fold to ASCII");
  okf(notes_text("H\xC3\xBClkenberg") == "H\xC3\xBClkenberg", "Latin-1 letters pass through (u-diaeresis)");
  okf(notes_text("a\xF0\x9F\x8F\x81z") == "a?z", "an emoji becomes one '?', not four garbage bytes");
  okf(notes_text("a\xE2\x80\xA6z") == "a...z", "an ellipsis becomes three dots");
  okf(notes_text("x\xC2\xA0y") == "x y", "a no-break space becomes a space");
  okf(notes_text("cut \xE2\x80") == "cut ??", "a truncated sequence becomes '?' marks, never a read past the end");
}

static void test_machine() {
  std::printf("the prompt follows the entity\n");
  okf(next_mode(M_CHECKING, E_AVAILABLE, 100) == M_AVAILABLE, "checking, then an update appears");
  okf(next_mode(M_CHECKING, E_NO_UPDATE, 1000) == M_CHECKING,
      "checking: a stale 'no update' is not believed within 1.5 s");
  okf(next_mode(M_CHECKING, E_NO_UPDATE, 1600) == M_LATEST, "...after that it is");
  okf(next_mode(M_CHECKING, E_UNKNOWN, 10000) == M_CHECKING, "no answer yet: keep waiting");
  okf(next_mode(M_CHECKING, E_UNKNOWN, 25001) == M_FAILED, "no answer in 25 s: couldn't check");
  okf(next_mode(M_CHECKING, E_NO_UPDATE, 25001) == M_LATEST, "...unless the answer was 'up to date'");
  okf(next_mode(M_INSTALLING, E_INSTALLING, 5000) == M_INSTALLING, "installing: stay");
  okf(next_mode(M_INSTALLING, E_AVAILABLE, 5000) == M_INSTALLING, "available again, but only 5 s in: still installing");
  okf(next_mode(M_INSTALLING, E_AVAILABLE, 120001) == M_AVAILABLE, "available again after 2 min: the install failed");
  okf(next_mode(M_LATEST, E_AVAILABLE, 99999) == M_LATEST, "a finished card is not reopened by the entity");
  okf(next_mode(M_NONE, E_AVAILABLE, 99999) == M_NONE, "nothing open: nothing opens");
}

static void test_icon() {
  std::printf("the icon beside the gear\n");
  okf(icon_wanted(E_AVAILABLE, M_NONE, true), "an update is waiting: icon");
  okf(!icon_wanted(E_AVAILABLE, M_NONE, false), "...but not on a page without the gear (settings, boot, About)");
  okf(!icon_wanted(E_AVAILABLE, M_INSTALLING, true), "not while installing");
  okf(icon_wanted(E_AVAILABLE, M_AVAILABLE, true), "still there under the open prompt");
  okf(!icon_wanted(E_NO_UPDATE, M_NONE, true) && !icon_wanted(E_UNKNOWN, M_NONE, true), "no update, no icon");
}

int main() {
  std::printf("\nF1 Tracker - firmware update tests\n\n");
  test_versions();
  test_newer_only();
  test_notes();
  test_machine();
  test_icon();
  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}

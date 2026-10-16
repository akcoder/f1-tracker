#pragma once
// Where the big tables live (decision 203). Internal RAM on the S3 is the scarce resource:
// Wi-Fi, the Bluetooth host and controller, lwIP and LVGL all want it, and on the first board
// the Bluetooth controller could not allocate its advertisement while this project's own
// static tables held ~57 kB of it. Tagged PSRAM_BSS, a table goes to PSRAM instead; nothing
// here is touched from an interrupt or by DMA, so the slower bus does not matter.
// Needs CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY (f1-tracker.yaml); without it, or on the
// host, the attribute is empty and the table stays where it was.
#if __has_include("esp_attr.h")
#include "esp_attr.h"
#define PSRAM_BSS EXT_RAM_BSS_ATTR
#else
#define PSRAM_BSS
#endif

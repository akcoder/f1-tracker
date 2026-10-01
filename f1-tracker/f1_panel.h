#pragma once
// HW-7: soft-reset the ST7701S before the SPI bus comes up.
//
// The GUITION-4848S040 drives its panel over a 16-bit parallel RGB bus, but the
// controller's registers are reached on a separate 3-wire SPI link. On a warm
// boot the controller can still hold the state the previous firmware left it
// in, and the mipi_rgb init sequence then runs against a half-configured chip:
// the symptom is a panel that comes up blank, shifted, or full of garbage lines
// on some boots and not others.
//
// sky-tracker (4.5.34, its HW-7) cures this by issuing SWRESET from on_boot at
// priority 1100 - BEFORE ESPHome's SPI bus (1000) claims the pins - and that is
// what this reproduces. plane-tracker omitted it and left it as the first thing
// to look at if the panel misbehaves; this project ports it up front instead.
//
// The link is 9-bit: one D/C bit (0 = command, 1 = data) then eight payload
// bits, MSB first, clocked in on the rising edge while CS is low. We drive the
// pins directly with the ROM GPIO calls because no ESPHome component owns them
// yet at priority 1100 - that is the entire point of running this early.
#include <cstdint>

#if defined(USE_ESP32) && !defined(F1_HOST_TEST)
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esphome/core/log.h"

namespace f1 {

namespace {
constexpr const char *PANEL_TAG = "f1_panel";
constexpr uint32_t SPI_HALF_PERIOD_US = 2;  // ~250 kHz; the ST7701S accepts far
                                            // faster, but this runs once at boot
constexpr uint8_t ST7701_SWRESET = 0x01;
constexpr uint32_t SWRESET_SETTLE_MS = 120;  // datasheet: 120 ms before the next
                                             // command after a software reset
}  // namespace

// Clock out one 9-bit frame: dc then eight bits of value, MSB first.
inline void panel_write9(gpio_num_t sck, gpio_num_t mosi, bool dc, uint8_t value) {
  const uint16_t frame = (uint16_t) (dc ? 0x100 : 0x000) | value;
  for (int bit = 8; bit >= 0; bit--) {
    gpio_set_level(mosi, (frame >> bit) & 1);
    esp_rom_delay_us(SPI_HALF_PERIOD_US);
    gpio_set_level(sck, 1);  // sampled on the rising edge
    esp_rom_delay_us(SPI_HALF_PERIOD_US);
    gpio_set_level(sck, 0);
  }
}

// HW-7. Pins are the panel's init-SPI trio, NOT the display data bus:
// cs=GPIO39, sck=GPIO48, mosi=GPIO47 on this board (REQUIREMENTS.md 2.1).
inline void panel_soft_reset(int cs_pin, int sck_pin, int mosi_pin) {
  const auto cs = (gpio_num_t) cs_pin, sck = (gpio_num_t) sck_pin,
             mosi = (gpio_num_t) mosi_pin;
  const uint64_t mask = (1ULL << cs_pin) | (1ULL << sck_pin) | (1ULL << mosi_pin);
  gpio_config_t io{};
  io.pin_bit_mask = mask;
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io);

  gpio_set_level(cs, 1);  // idle high
  gpio_set_level(sck, 0);
  gpio_set_level(mosi, 0);
  esp_rom_delay_us(10);

  gpio_set_level(cs, 0);
  panel_write9(sck, mosi, false, ST7701_SWRESET);
  gpio_set_level(cs, 1);

  // Block here rather than scheduling: the SPI bus component is initialised at
  // priority 1000, a few hundred microseconds from now, and it must not find a
  // controller mid-reset.
  esp_rom_delay_us(SWRESET_SETTLE_MS * 1000);

  // Hand the pins back floating so the SPI bus can configure them itself.
  gpio_reset_pin(cs);
  gpio_reset_pin(sck);
  gpio_reset_pin(mosi);

  ESP_LOGI(PANEL_TAG, "HW-7: ST7701S soft reset on cs=%d sck=%d mosi=%d", cs_pin,
           sck_pin, mosi_pin);
}

}  // namespace f1
#else
namespace f1 {
inline void panel_write9(int, int, bool, uint8_t) {}
inline void panel_soft_reset(int, int, int) {}
}  // namespace f1
#endif

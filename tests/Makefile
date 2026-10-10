# Host tests (REQUIREMENTS.md section 10).  make -C tests
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter
HDRS     := $(wildcard ../f1-tracker/*.h)
BINS     := test_m1 test_state test_json test_watch test_birthday test_display test_champ test_team test_extras test_rollover test_net test_boot test_sun test_update

# test_jpg needs LVGL's TJpgDec from an ESPHome build (see the end of this file)
TJPG_SRC := $(firstword $(wildcard ../.esphome/build/f1-tracker/managed_components/lvgl__lvgl/src/libs/tjpgd))
ifneq ($(TJPG_SRC),)
BINS += test_jpg
endif

.PHONY: all clean
all: $(BINS)
	@for b in $(BINS); do ./$$b || exit 1; done

test_m1: test_m1.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_state: test_state.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -rf $(BINS) .tjpgd

test_json: test_json.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_watch: test_watch.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_birthday: test_birthday.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_display: test_display.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_champ: test_champ.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_team: test_team.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_extras: test_extras.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_rollover: test_rollover.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_net: test_net.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_boot: test_boot.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_sun: test_sun.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_update: test_update.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

# f1_jpg.h's host build uses LVGL's TJpgDec from the last ESPHome build (the .esphome
# folder is git-ignored). Its header pulls in lv_conf_internal.h, so a copy without
# that line is made here.

.tjpgd/tjpgd.c: $(TJPG_SRC)/tjpgd.c
	mkdir -p .tjpgd
	cp $(TJPG_SRC)/tjpgd.c $(TJPG_SRC)/tjpgdcnf.h .tjpgd/
	sed '/lv_conf_internal.h/d' $(TJPG_SRC)/tjpgd.h > .tjpgd/tjpgd.h

test_jpg: test_jpg.cpp ../f1-tracker/f1_jpg.h ../f1-tracker/f1_portraits.h .tjpgd/tjpgd.c
	$(CC) -O1 -DLV_USE_TJPGD=1 -c -o .tjpgd/tjpgd.o .tjpgd/tjpgd.c
	$(CXX) $(CXXFLAGS) -DLV_USE_TJPGD=1 -I.tjpgd -o $@ test_jpg.cpp .tjpgd/tjpgd.o

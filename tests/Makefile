# Host tests (REQUIREMENTS.md section 10).  make -C tests
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter
HDRS     := $(wildcard ../f1-tracker/*.h)
BINS     := test_m1 test_state test_json test_watch test_birthday test_display test_champ test_team test_extras test_rollover test_net test_boot test_sun test_update

.PHONY: all clean
all: $(BINS)
	@for b in $(BINS); do ./$$b || exit 1; done

test_m1: test_m1.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

test_state: test_state.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(BINS)

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

# Host tests (REQUIREMENTS.md section 10).  make -C tests
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter
BIN      := test_m1

.PHONY: all clean
all: $(BIN)
	./$(BIN)

$(BIN): test_m1.cpp ../f1-tracker/f1_map.h ../f1-tracker/f1_carousel.h \
        ../f1-tracker/f1_circuits.h ../f1-tracker/f1_calendar.h
	$(CXX) $(CXXFLAGS) -o $@ test_m1.cpp

clean:
	rm -f $(BIN)

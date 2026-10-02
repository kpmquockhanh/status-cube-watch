# Desktop simulator for the status cube UI.
#
#   brew install sdl2 && make run
#
# Reuses the libraries PlatformIO already downloaded, so run `pio run` once
# first (or point LIBDEPS at your own checkouts of LovyanGFX / ArduinoJson).

LIBDEPS ?= ../.pio/libdeps/waveshare-s3-lcd169
LGFX    := $(LIBDEPS)/LovyanGFX/src
AJSON   := $(LIBDEPS)/ArduinoJson/src

BIN     := build/cube-sim
SCALE   ?= 2
POMO_FAST ?= 1

# sim/ comes first on the include path so <Arduino.h> finds the desktop shim.
CXXFLAGS := -std=c++17 -O2 -DLGFX_SDL -DSIM_SCALE=$(SCALE) -DPOMO_TIME_DIV=$(POMO_FAST) \
            -I. -I$(LGFX) -I$(AJSON) $(shell pkg-config --cflags sdl2) \
            -Wno-deprecated-declarations
CFLAGS   := -O2 -I$(LGFX) $(shell pkg-config --cflags sdl2)
LDFLAGS  := $(shell pkg-config --libs sdl2) -lpthread

# The firmware sources under test, plus the desktop stand-ins for the two
# layers that touch hardware (WiFi and I2C).
SRCS := ../src/main.cpp ../src/ui.cpp ../src/payload.cpp ../src/gesture.cpp ../src/pomodoro.cpp ../src/pomo_editor.cpp \
        net_sim.cpp touch_sim.cpp settings_sim.cpp portal_sim.cpp ota_sim.cpp battery_sim.cpp sdl_main.cpp

# The headless frame grab shares ui.cpp and payload.cpp but brings its own
# entry point, and needs no network or touch.
SHOT     := build/cube-shot
SHOT_OBJ := build/shot.o build/ui.o build/payload.o build/pomodoro.o build/pomo_editor.o
LGFX_CPP := $(wildcard $(LGFX)/lgfx/v1/*.cpp)
LGFX_C   := $(wildcard $(LGFX)/lgfx/Fonts/efont/*.c) \
            $(wildcard $(LGFX)/lgfx/Fonts/IPA/*.c) \
            $(wildcard $(LGFX)/lgfx/Fonts/lvgl/*.c) \
            $(wildcard $(LGFX)/lgfx/utility/*.c) \
            $(wildcard $(LGFX)/lgfx/v1/lv_font/*.c)

OBJS := $(addprefix build/,$(notdir $(SRCS:.cpp=.o))) \
        $(patsubst $(LGFX)/%.cpp,build/lgfx/%.o,$(LGFX_CPP)) \
        $(patsubst $(LGFX)/%.c,build/lgfx/%.o,$(LGFX_C))

VPATH := ../src:.

all: $(BIN)

$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@
	@echo "built $@"

build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/lgfx/%.o: $(LGFX)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/lgfx/%.o: $(LGFX)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

$(SHOT): $(SHOT_OBJ) $(filter build/lgfx/%,$(OBJS))
	@mkdir -p $(dir $@)
	$(CXX) $^ $(LDFLAGS) -lz -o $@
	@echo "built $@"

# Grabs one PNG per card straight from the renderer. Point SHOT_URL at a
# different bridge, or SHOT_JSON at a saved payload, to shoot other states.
SHOT_URL ?= http://localhost:8787/api/status
SHOT_OUT ?= build/shot
shot: $(SHOT)
	@curl -sf $(SHOT_URL) > build/payload.json || \
	  { echo "no payload from $(SHOT_URL)"; exit 1; }
	./$(SHOT) $(SHOT_OUT) build/payload.json

# Host-side unit tests for the logic that has no hardware in it. A plain
# compiler: no SDL, LovyanGFX or Arduino. Each test links only the sources it
# is given an extra rule for below.
TESTS := build/portal_util_test build/pomodoro_test build/gesture_test build/deck_test \
         build/pomo_settings_test build/pomo_editor_test build/battery_util_test \
         build/ble_frame_test

build/pomodoro_test: ../src/pomodoro.cpp
build/gesture_test: ../src/gesture.cpp
build/pomo_editor_test: ../src/pomo_editor.cpp

build/%_test: tests/%_test.cpp
	@mkdir -p build
	$(CXX) -std=c++17 -Wall -Wextra -I../src -Itests $(filter %.cpp,$^) -o $@

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

clean:
	rm -rf build

.PHONY: all run shot test clean

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
SRCS := ../src/main.cpp ../src/ui.cpp ../src/payload.cpp ../src/gesture.cpp ../src/pomodoro.cpp ../src/pomo_editor.cpp ../src/dev_editor.cpp ../src/settings_json.cpp \
        net_sim.cpp touch_sim.cpp settings_sim.cpp portal_sim.cpp ota_sim.cpp battery_sim.cpp ble_sim.cpp imu_sim.cpp buzzer_sim.cpp sdl_main.cpp

# The headless frame grab shares ui.cpp and payload.cpp but brings its own
# entry point, and needs no network or touch.
SHOT     := build/cube-shot
SHOT_OBJ := build/shot.o build/ui.o build/payload.o build/pomodoro.o build/pomo_editor.o build/dev_editor.o build/settings_sim.o
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

# -MMD -MP: each object also writes a .d file listing the headers it read, so
# editing a header (several modules are header-only) rebuilds what includes it
# instead of reusing a stale object. The tests get the same below.
build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

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

# Performance and energy bench (bench.cpp): the real setup()/loop() on a
# virtual clock against scripted touch/BLE/WiFi, with a windowless panel that
# counts every pixel sent. Built apart in build/bench/ because the flags change
# Arduino.h and display.h.
BENCH      := build/cube-bench
BENCH_SRCS := ../src/main.cpp ../src/ui.cpp ../src/payload.cpp ../src/gesture.cpp ../src/pomodoro.cpp \
              ../src/pomo_editor.cpp ../src/dev_editor.cpp ../src/settings_json.cpp \
              settings_sim.cpp portal_sim.cpp ota_sim.cpp bench.cpp
BENCH_OBJ  := $(addprefix build/bench/,$(notdir $(BENCH_SRCS:.cpp=.o)))
BENCH_RUNS ?= desk linked pomodoro browse editor sleep volume

build/bench/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -DSIM_VIRTUAL_CLOCK -DSIM_BENCH -MMD -MP -c $< -o $@

$(BENCH): $(BENCH_OBJ) $(filter build/lgfx/%,$(OBJS))
	@mkdir -p $(dir $@)
	$(CXX) $^ $(LDFLAGS) -o $@
	@echo "built $@"

bench: $(BENCH)
	@./$(BENCH) header
	@for s in $(BENCH_RUNS); do ./$(BENCH) $$s || exit 1; done
	@echo
	@./$(BENCH) render

# Host-side unit tests for the logic that has no hardware in it. A plain
# compiler: no SDL, LovyanGFX or Arduino. Each test links only the sources it
# is given an extra rule for below.
TESTS := build/portal_util_test build/pomodoro_test build/gesture_test build/deck_test \
         build/pomo_settings_test build/pomo_editor_test build/battery_util_test \
         build/ble_frame_test build/transport_policy_test build/idle_sleep_test build/device_settings_test \
         build/settings_json_test build/dev_editor_test build/orientation_test build/touch_map_test \
         build/ble_auth_test build/age_label_test build/cpu_policy_test build/readings_key_test \
         build/ble_conn_test build/touch_gate_test build/melody_test \
         build/volume_frame_test build/volume_slider_test build/unlock_prompt_test

build/pomodoro_test: ../src/pomodoro.cpp
build/gesture_test: ../src/gesture.cpp
build/pomo_editor_test: ../src/pomo_editor.cpp
build/dev_editor_test: ../src/dev_editor.cpp ../src/pomo_editor.cpp
build/settings_json_test: ../src/settings_json.cpp tests/settings_fake.cpp

TEST_FLAGS := -std=c++17 -Wall -Wextra -I../src -Itests -I. -I$(AJSON)

# A test builds from several sources in one command, where -MMD would keep only
# the last one's headers, so its .d comes from a separate -MM pass over all.
# The sources are dropped from it: the rules above own them, and a stale .d
# would otherwise put a source the test no longer uses on the command line.
build/%_test: tests/%_test.cpp
	@mkdir -p build
	$(CXX) $(TEST_FLAGS) $(filter %.cpp,$^) -o $@
	@$(CXX) $(TEST_FLAGS) -MM -MP -MT $@ $(filter %.cpp,$^) | sed -E 's/ [^ ]+\.cpp//g' > $@.d

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

clean:
	rm -rf build

-include $(wildcard build/*.d build/bench/*.d)

.PHONY: all run shot test bench clean

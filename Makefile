ifeq ($(OS),Windows_NT)
# ---- Windows: builds WirelessBattery.exe with MinGW-w64 (MSYS2 MINGW64/UCRT64 shell, or any mingw g++ on PATH).
CXX      ?= g++
WINDRES  ?= windres
CXXFLAGS ?= -std=c++17 -Os -Wall -Wno-missing-field-initializers -ffunction-sections -fdata-sections \
            -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00
LDFLAGS  ?= -municode -mwindows -static -s -Wl,--gc-sections
LIBS     := -lhid -lsetupapi -lcfgmgr32 -lole32 -lcomctl32 -lgdi32 -lruntimeobject

SRC := $(wildcard src/*.cpp) $(wildcard src/drivers/*.cpp)
OBJ := $(patsubst src/%.cpp,build/%.o,$(SRC)) build/app_res.o

WirelessBattery.exe: $(OBJ)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)

build/%.o: src/%.cpp $(wildcard src/*.h src/drivers/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -municode -c $< -o $@

build/app_res.o: res/app.rc res/app.manifest res/app.ico
	@mkdir -p build
	$(WINDRES) -i $< -o $@ --include-dir res

clean:
	rm -rf build WirelessBattery.exe

test:
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -Wall tests/rapoo_protocol_test.cpp -o build/rapoo_protocol_test.exe
	./build/rapoo_protocol_test.exe
	$(CXX) $(CXXFLAGS) tests/rapoo_driver_test.cpp src/drivers/rapoo.cpp -o build/rapoo_driver_test.exe
	./build/rapoo_driver_test.exe

.PHONY: clean test

else
# ---- Linux: builds ./wireless-battery (needs g++, make, pkg-config, libglib2.0-dev and libudev-dev; see README).
CXX      ?= g++
PKG      := gio-2.0 libudev
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wno-missing-field-initializers -ffunction-sections -fdata-sections
CXXFLAGS += $(shell pkg-config --cflags $(PKG))
LDFLAGS  ?= -Wl,--gc-sections -static-libstdc++ -static-libgcc
LIBS     := $(shell pkg-config --libs $(PKG)) -lpthread

# Windows-only sources are left out; src/linux/ provides their replacements.
SRC := src/devices.cpp src/hid_common.cpp \
       $(filter-out src/drivers/xinput.cpp src/drivers/generic.cpp src/drivers/atk_serial.cpp,$(wildcard src/drivers/*.cpp)) \
       $(wildcard src/linux/*.cpp)
OBJ := $(patsubst src/%.cpp,build/linux/%.o,$(SRC))

PREFIX ?= $(HOME)/.local

wireless-battery: $(OBJ)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LIBS)

build/linux/%.o: src/%.cpp $(wildcard src/*.h src/drivers/*.h src/linux/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf build wireless-battery

test:
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -Wall tests/rapoo_protocol_test.cpp -o build/rapoo_protocol_test
	./build/rapoo_protocol_test
	$(CXX) $(CXXFLAGS) tests/rapoo_driver_test.cpp src/drivers/rapoo.cpp -o build/rapoo_driver_test
	./build/rapoo_driver_test

install: wireless-battery
	./packaging/install.sh --prefix "$(PREFIX)"

.PHONY: clean test install
endif

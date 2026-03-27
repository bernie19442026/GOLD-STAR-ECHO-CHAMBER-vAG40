CXX = clang++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -I src
OBJCXXFLAGS = $(CXXFLAGS) -fobjc-arc
FRAMEWORKS = -framework Accelerate -framework CoreAudio -framework AudioToolbox \
             -framework CoreFoundation -framework Cocoa -framework UniformTypeIdentifiers \
             -framework AVFoundation -framework QuartzCore

AUDIO_SRCS = src/audio/convolver.cpp \
             src/audio/ir_loader.cpp \
             src/audio/parameters.cpp \
             src/audio/audio_engine.cpp

STANDALONE_SRCS = src/standalone/audio_io.cpp \
                  src/standalone/standalone_app.cpp \
                  src/standalone/file_player.cpp

STANDALONE_MM = src/standalone/standalone_ui.mm \
                src/standalone/main.mm

AUDIO_OBJS = $(AUDIO_SRCS:.cpp=.o)
STANDALONE_OBJS = $(STANDALONE_SRCS:.cpp=.o)
STANDALONE_MM_OBJS = $(STANDALONE_MM:.mm=.o)

TARGET = build/GoldStarEchoChamber

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(AUDIO_OBJS) $(STANDALONE_OBJS) $(STANDALONE_MM_OBJS) | build
	$(CXX) $(CXXFLAGS) $(FRAMEWORKS) -o $@ $^

build:
	mkdir -p build

src/audio/%.o: src/audio/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/standalone/%.o: src/standalone/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/standalone/%.o: src/standalone/%.mm
	$(CXX) $(OBJCXXFLAGS) -c -o $@ $<

clean:
	rm -f $(AUDIO_OBJS) $(STANDALONE_OBJS) $(STANDALONE_MM_OBJS)
	rm -rf build/GoldStarEchoChamber

run: $(TARGET)
	./$(TARGET)

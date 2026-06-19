# Makefile for MorphOS (PowerPC GCC)
# Attack of the PETSCII Robots - MorphOS RTG + Reggae port

CC = ppc-morphos-g++
STRIP = ppc-morphos-strip
CFLAGS = -O2 -Wall -Wno-narrowing \
	 -DPLATFORM_NAME=\"morphos\" \
	 -DPLATFORM_SCREEN_WIDTH=320 \
	 -DPLATFORM_SCREEN_HEIGHT=200 \
	 -DPLATFORM_MAP_WINDOW_TILES_WIDTH=11 \
	 -DPLATFORM_MAP_WINDOW_TILES_HEIGHT=7 \
	 -DPLATFORM_MODULE_BASED_AUDIO \
	 -DPLATFORM_IMAGE_BASED_TILES \
	 -DPLATFORM_IMAGE_SUPPORT \
	 -DPLATFORM_SPRITE_SUPPORT \
	 -DPLATFORM_COLOR_SUPPORT \
	 -DPLATFORM_CURSOR_SUPPORT \
	 -DPLATFORM_CURSOR_SHAPE_SUPPORT \
	 -DPLATFORM_FADE_SUPPORT \
	 -DPLATFORM_LIVE_MAP_SUPPORT \
	 -DPLATFORM_PRELOAD_SUPPORT \
	 -DOPTIMIZED_MAP_RENDERING \
	 -DPLATFORM_MAP_COUNT=14

LDFLAGS =
LIBS = -lauto

OBJS = petrobots.o Platform.o PlatformMorphOS.o Palette.o

TARGET = PETSCIIRobots

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	$(STRIP) $@

petrobots.o: petrobots.cpp PlatformMorphOS.h petrobots.h Platform.h
	$(CC) $(CFLAGS) -c -o $@ petrobots.cpp

Platform.o: Platform.cpp Platform.h
	$(CC) $(CFLAGS) -c -o $@ Platform.cpp

PlatformMorphOS.o: PlatformMorphOS.cpp PlatformMorphOS.h Platform.h Palette.h
	$(CC) $(CFLAGS) -c -o $@ PlatformMorphOS.cpp

Palette.o: Palette.cpp Palette.h Platform.h
	$(CC) $(CFLAGS) -c -o $@ Palette.cpp

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	copy $(TARGET) RAM:/

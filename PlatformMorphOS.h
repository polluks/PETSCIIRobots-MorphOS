#ifndef _PLATFORMMORPHOS_H
#define _PLATFORMMORPHOS_H

#define PlatformClass PlatformMorphOS

#include "Platform.h"

struct Screen;
struct Window;
struct BitMap;
struct RastPort;
struct MsgPort;
struct IOAudio;
struct Library;
class Palette;

class PlatformMorphOS : public Platform {
public:
    PlatformMorphOS();
    ~PlatformMorphOS();

    virtual uint8_t* standardControls() const;
    virtual void setInterrupt(void (*interrupt)(void));
    virtual void show();
    virtual int framesPerSecond();
    virtual uint8_t readKeyboard();
    virtual void keyRepeat();
    virtual void clearKeyBuffer();
    virtual bool isKeyOrJoystickPressed(bool gamepad);
    virtual uint16_t readJoystick(bool gamepad);
    virtual void displayImage(Image image);
    virtual void loadMap(Map map, uint8_t* destination);
    virtual uint8_t* loadTileset();
    virtual void generateTiles(uint8_t* tileData, uint8_t* tileAttributes);
    virtual void renderTile(uint8_t tile, uint16_t x, uint16_t y, uint8_t variant = 0, bool transparent = false);
    virtual void renderTiles(uint8_t backgroundTile, uint8_t foregroundTile, uint16_t x, uint16_t y, uint8_t backgroundVariant = 0, uint8_t foregroundVariant = 0);
    virtual void renderItem(uint8_t item, uint16_t x, uint16_t y);
    virtual void renderKey(uint8_t key, uint16_t x, uint16_t y);
    virtual void renderHealth(uint8_t health, uint16_t x, uint16_t y);
    virtual void renderFace(uint8_t face, uint16_t x, uint16_t y);
    virtual void renderLiveMap(uint8_t* map);
    virtual void renderLiveMapTile(uint8_t* map, uint8_t x, uint8_t y);
    virtual void renderLiveMapUnits(uint8_t* map, uint8_t* unitTypes, uint8_t* unitX, uint8_t* unitY, uint8_t playerColor, bool showRobots);
    virtual void showCursor(uint16_t x, uint16_t y);
    virtual void hideCursor();
    virtual void setCursorShape(CursorShape shape);
    virtual void copyRect(uint16_t sourceX, uint16_t sourceY, uint16_t destinationX, uint16_t destinationY, uint16_t width, uint16_t height);
    virtual void clearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
    virtual void fillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color);
    virtual void startShakeScreen();
    virtual void shakeScreen();
    virtual void stopShakeScreen();
    virtual void startFadeScreen(uint16_t color, uint16_t intensity);
    virtual void fadeScreen(uint16_t intensity, bool immediate = true);
    virtual void stopFadeScreen();
    virtual void writeToScreenMemory(address_t address, uint8_t value);
    virtual void writeToScreenMemory(address_t address, uint8_t value, uint8_t color, uint8_t yOffset);
    virtual void playNote(uint8_t note);
    virtual void stopNote();
    virtual void loadModule(Module module);
    virtual void playModule(Module module);
    virtual void pauseModule();
    virtual void stopModule();
    virtual void playSample(uint8_t sample);
    virtual void stopSample();
    virtual void renderFrame(bool waitForNextFrame = false);
    virtual void waitForScreenMemoryAccess();
    virtual void setHighlightedMenuRow(uint16_t row);
    virtual void rumble(uint8_t strength);

private:
    uint32_t loadFile(const char* filename, uint8_t* destination, uint32_t size);
    void loadRawFile(const char* filename, uint8_t* destination, uint32_t size);
    void loadAssets();
    void loadSamples();

    // Planar (4-bitplane interleaved) to chunky (8-bit) conversion
    void planarToChunky8(const uint8_t* planar, uint8_t* chunky, uint16_t width, uint16_t height);
    void planarToChunky24(const uint8_t* planar, uint8_t* chunky, uint16_t width, uint16_t height, uint16_t numTiles);
    void convertTileData();

    void chunkyBlit(const uint8_t* source, uint16_t dx, uint16_t dy,
                    uint16_t w, uint16_t h, bool transparent, uint8_t transparentColor);

    // Audio via Reggae (multimedia.class BOOPSI API)
    void initAudio();
    void cleanupAudio();

    int framesPerSecond_;
    Screen* screen;
    Window* window;
    BitMap* screenBitMap;
    uint8_t* screenMemory;    // Amiga-style screen planes for displayImage
    uint8_t* chunkyBuffer;    // 320x200 8-bit chunky framebuffer
    uint8_t* fontData;        // 256 chars x 8 bytes (C64 font glyphs)

    // Pre-rendered chunky tile data
    uint8_t* tileChunky[256];        // 256 tiles x (24x24) or null
    bool tileHasTransparency[256];
    int8_t animTileMap[256];         // Map tile -> anim tile index

    // Chunky surfaces for HUD elements
    uint8_t* itemChunky;
    uint16_t itemWidth, itemHeight, itemCount;
    uint8_t* keyChunky;
    uint16_t keyWidth, keyHeight, keyCount;
    uint8_t* healthChunky;
    uint16_t healthWidth, healthHeight, healthCount;
    uint8_t* faceChunky;
    uint16_t faceWidth, faceHeight, faceCount;
    uint8_t* spriteChunky;
    uint16_t spriteWidth, spriteHeight, spriteCount;
    uint8_t* animTileChunky;

    // Tile mask data (original planar masks)
    uint8_t* tilesMask;

    // Live map rendering
    uint8_t tileLiveMap[256];
    uint8_t liveMapBuffer[128 * 64];

    // Cursor state
    int cursorX_, cursorY_;
    bool cursorVisible_;
    CursorShape cursorShape_;
    uint8_t cursorData[28 * 32];

    // Audio state
    uint8_t* moduleData;
    Module loadedModule;
    Library* MultimediaBase;        // multimedia/multimedia.class
    Library* AudioOutputBase;       // multimedia/audio.output
    void* moduleMedia;              // Object* - Reggae media (file.stream)
    void* moduleOutput;             // Object* - audio.output instance
    bool audioInitialized_;

    // Sample cache (raw PCM wrapped as WAV, played via Reggae memory.stream)
    static const int NUM_SAMPLES = 16;
    uint8_t* sampleCache[NUM_SAMPLES];
    uint32_t sampleSizes[NUM_SAMPLES];
    bool sampleLoaded[NUM_SAMPLES];
    void* sampleMedia;
    void* sampleOutput;

    // Shake
    uint8_t shakeStep_;
    int16_t shakeOffsetX_;

    // Fade
    uint16_t fadeIntensity_;

    // Input
    uint8_t keyToReturn_;
    uint8_t downKey_;
    uint8_t shift_;
    uint16_t joystickStateToReturn_;
    uint16_t joystickState_;
    uint16_t pendingState_;
    Library* lowLevelBase_;
    bool joystickConfigured_;
    uint32_t joystickPort_;

    // Interrupt callback
    void (*interrupt_)(void);

    // Timing
    uint32_t clock_;
    uint32_t frameCount_;

    // Palette
    Palette* palette;
    uint16_t amigaPalette[16];

    // Asset load buffer
    uint8_t* loadBuffer_;
    uint32_t loadBufferSize_;
    bool preloaded_;
    uint8_t* preloadedModuleData_[8];
    uint32_t preloadedModuleLengths_[8];
};

#endif

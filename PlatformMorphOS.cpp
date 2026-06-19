/**
 * PlatformMorphOS - MorphOS RTG + Reggae implementation
 * Uses cybergraphics.library for RTG framebuffer access,
 * and reggae.library (digibooster3.demuxer) for audio.
 */

#include <exec/exec.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/multimedia.h>
#include <classes/multimedia/multimedia.h>
#include <proto/lowlevel.h>
#include <libraries/lowlevel.h>
#include <proto/xadmaster.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "Palette.h"
#include "PlatformMorphOS.h"

// Missing definitions from MorphOS SDK (if not provided by headers)
#ifndef IDCMP_JOYSTICK
#define IDCMP_JOYSTICK 0x200000
#endif

#ifndef JPAT_Type
#define JPAT_Type        (TAG_USER + 1)
#define JPTYPE_GAMEPAD   4
#define JPAT_RumbleMotor (TAG_USER + 2)
#define JPAT_ButtonState (TAG_USER + 3)
#define JPAT_RumbleDuration (TAG_USER + 4)
#endif

#ifndef JPF_BUTTON_PLAY
#define JPF_BUTTON_PLAY    0x01
#define JPF_BUTTON_REVERSE 0x02
#define JPF_BUTTON_FORWARD 0x04
#define JPF_BUTTON_EXTRA   0x08
#define JPF_JOY_UP         0x0100
#define JPF_JOY_DOWN       0x0200
#define JPF_JOY_LEFT       0x0400
#define JPF_JOY_RIGHT      0x0800
#endif

// XAD decompression via xadmaster.library.
static bool xadDecodeFile(const char* gzPath, uint8_t* destination, uint32_t size)
{
    BPTR file = Open((STRPTR)gzPath, MODE_OLDFILE);
    if (!file) return false;

    struct FileInfoBlock fib;
    if (!ExamineFH(file, &fib)) { Close(file); return false; }

    uint32_t csize = fib.fib_Size;
    uint8_t* cbuf = (uint8_t*)AllocVec(csize, MEMF_CLEAR);
    if (!cbuf) { Close(file); return false; }

    Read(file, cbuf, csize);
    Close(file);

    struct xadArchiveInfo* arch = (struct xadArchiveInfo*)xadAllocObjectA(XADOBJ_ARCHIVEINFO, NULL);
    if (!arch) { FreeVec(cbuf); return false; }

    struct TagItem infoTags[3] = {
        {XAD_INMEMORY, (ULONG)cbuf},
        {XAD_INSIZE,   (ULONG)csize},
        {TAG_DONE, 0}
    };

    bool ok = false;
    if (xadGetInfoA(arch, infoTags) == XADERR_OK) {
        if (arch->xai_FileInfo && arch->xai_FileInfo->xfi_Size == (xadSize)size) {
            struct TagItem outTags[3] = {
                {XAD_OUTMEMORY, (ULONG)destination},
                {XAD_OUTSIZE,   (ULONG)size},
                {TAG_DONE, 0}
            };
            ok = (xadFileUnArcA(arch, outTags) == XADERR_OK);
        }
    }

    xadFreeInfo(arch);
    xadFreeObjectA(arch, NULL);
    FreeVec(cbuf);
    return ok;
}

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define SCREEN_WIDTH_IN_CHARS (SCREEN_WIDTH / 8)
#define SCREEN_HEIGHT_IN_CHARS (SCREEN_HEIGHT / 8)
#define PLANES 4
#define FONT_SIZE (256 * 8)
#define TILE_PIXEL_SIZE (24 * 24)
#define NUM_TILES 256
#define ANIM_TILES 17
#define SPRITE_COUNT 77

// Asset paths relative to PROGDIR:
static const char* FONT_PATH = "Amiga/C64Font.raw";
static const char* TILES_PATH = "Amiga/Tiles.raw";
static const char* ANIM_TILES_PATH = "Amiga/AnimTiles.raw";
static const char* FACES_PATH = "Amiga/Faces.raw";
static const char* ITEMS_PATH = "Amiga/Items.raw";
static const char* KEYS_PATH = "Amiga/Keys.raw";
static const char* HEALTH_PATH = "Amiga/Health.raw";
static const char* SPRITES_PATH = "Amiga/Sprites.raw";
static const char* SPRITES_MASK_PATH = "Amiga/SpritesMask.raw";
static const char* TILESET_PATH = "tileset.amiga";

static const char* IMAGE_PATHS[] = {
    "Amiga/Data/IntroScreen.raw",
    "Amiga/Data/GameScreen.raw",
    "Amiga/Data/GameOver.raw"
};

static const char* MODULE_PATHS[] = {
    "Music/mod.metal heads",
    "Music/mod.win",
    "Music/mod.lose",
    "Music/mod.metallic bop amiga",
    "Music/mod.get psyched",
    "Music/mod.robot attack",
    "Music/mod.rushin in",
    "Music/mod.soundfx"
};

static const char* SFX_PATHS[] = {
    "Sounds/sounds_dsbarexp.raw",
    "Sounds/SOUND_BEEP.raw",
    "Sounds/SOUND_MEDKIT.raw",
    "Sounds/SOUND_EMP.raw",
    "Sounds/SOUND_MAGNET2.raw",
    "Sounds/SOUND_SHOCK.raw",
    "Sounds/SOUND_MOVE.raw",
    "Sounds/SOUND_PLASMA_FASTER.raw",
    "Sounds/sounds_dspistol.raw",
    "Sounds/SOUND_FOUND_ITEM.raw",
    "Sounds/SOUND_ERROR.raw",
    "Sounds/SOUND_CYCLE_WEAPON.raw",
    "Sounds/SOUND_CYCLE_ITEM.raw",
    "Sounds/SOUND_DOOR_FASTER.raw",
    "Sounds/SOUND_BEEP2.raw",
    "Amiga/SquareWave.raw"
};

static char notEnoughMemoryError[] = "Not enough memory to run\n";
static char unableToInitDisplay[] = "Unable to initialize display\n";
//static char unableToInitAudio[] = "Unable to initialize audio\n";
static char unableToLoadData[] = "Unable to load data\n";

static uint8_t standardControls[] = {
    0x17, 0x27, 0x26, 0x28, 0x11, 0x21, 0x20, 0x22,
    0x50, 0x51, 0x40, 0x31, 0x37, 0x42, 0xc2, 0x45,
    0x55, 0xb3, 0x4c, 0x4d, 0x4f, 0x4e, 0x40, 0x44,
    0x15, 0x36
};

// Anim tile map (matches Amiga version)
static int8_t animTileMapDefault[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1,  0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 16,
    -1, -1, -1, -1,  4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1,  8, 10, -1, -1, 12, 14, -1, -1, 20, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

// Sprite tile map (matches Amiga version)
static int8_t spriteTileMap[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     0,  1, 49, 50, 57, 58, 59, 60, -1, -1, -1, -1, -1, -1, -1, 48,
    -1, -1, -1, 73, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
     1,  0,  3, -1, 53, 54, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, 76, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

static uint16_t blackPalette[16] = { 0 };

static char MAPNAME[] = "Amiga/Data/level-a";
//static char gzExtension[] = ".gz";

// Tileset data loaded from tileset.amiga
static uint8_t tilesetData[514];
//static uint8_t* g_tileset = tilesetData;

// Address map for screen character -> pixel offset
static uint32_t addressMap[SCREEN_WIDTH_IN_CHARS * SCREEN_HEIGHT_IN_CHARS];

// ============================================================
// Planar (4-bitplane interleaved) to 8-bit chunky conversion
// ============================================================
static void planarToChunky(const uint8_t* src, uint8_t* dst, int width, int height)
{
    int bytesPerRow = (width + 7) / 8;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t color = 0;
            for (int plane = 0; plane < PLANES; plane++) {
                int byteOffset = (y * bytesPerRow * PLANES) + (plane * bytesPerRow) + (x / 8);
                int bit = 7 - (x & 7);
                if (src[byteOffset] & (1 << bit)) {
                    color |= (1 << plane);
                }
            }
            dst[y * width + x] = color;
        }
    }
}

// ============================================================
// PlatformMorphOS implementation
// ============================================================

PlatformMorphOS::PlatformMorphOS() :
    framesPerSecond_(50),
    screen(0),
    window(0),
    screenBitMap(0),
    screenMemory(0),
    chunkyBuffer(0),
    fontData(0),
    itemChunky(0),
    itemWidth(0),
    itemHeight(0),
    itemCount(0),
    keyChunky(0),
    keyWidth(0),
    keyHeight(0),
    keyCount(0),
    healthChunky(0),
    healthWidth(0),
    healthHeight(0),
    healthCount(0),
    faceChunky(0),
    faceWidth(0),
    faceHeight(0),
    faceCount(0),
    spriteChunky(0),
    spriteWidth(0),
    spriteHeight(0),
    spriteCount(0),
    animTileChunky(0),
    tilesMask(0),
    cursorX_(0),
    cursorY_(0),
    cursorVisible_(false),
    cursorShape_(ShapeUse),
    moduleData(0),
    loadedModule(ModuleSoundFX),
    MultimediaBase(0),
    AudioOutputBase(0),
    moduleMedia(0),
    moduleOutput(0),
    audioInitialized_(false),
    sampleMedia(0),
    sampleOutput(0),
    shakeStep_(0),
    shakeOffsetX_(0),
    fadeIntensity_(15),
    keyToReturn_(0xff),
    downKey_(0xff),
    shift_(0),
    joystickStateToReturn_(0),
    joystickState_(0),
    pendingState_(0),
    lowLevelBase_(0),
    joystickConfigured_(false),
    joystickPort_(0),
    interrupt_(0),
    clock_(0),
    frameCount_(0),
    palette(0),
    loadBuffer_(0),
    loadBufferSize_(0),
    preloaded_(false)
{
    memset(tileChunky, 0, sizeof(tileChunky));
    memset(tileHasTransparency, 0, sizeof(tileHasTransparency));
    memcpy(animTileMap, animTileMapDefault, sizeof(animTileMap));
    memset(amigaPalette, 0, sizeof(amigaPalette));
    memset(preloadedModuleData_, 0, sizeof(preloadedModuleData_));
    memset(preloadedModuleLengths_, 0, sizeof(preloadedModuleLengths_));

    // Initialize palette system
    Palette::initialize();
    palette = new Palette(blackPalette, 16, 0);

    // Build address map
    for (int y = 0, i = 0; y < SCREEN_HEIGHT_IN_CHARS; y++) {
        for (int x = 0; x < SCREEN_WIDTH_IN_CHARS; x++, i++) {
            addressMap[i] = y * SCREEN_WIDTH * 8 + x * 8;
        }
    }

    // Set up display
    struct NewScreen ns = {0};
    ns.LeftEdge = 0;
    ns.TopEdge = 0;
    ns.Width = SCREEN_WIDTH;
    ns.Height = SCREEN_HEIGHT;
    ns.Depth = 8;             // 8-bit chunky
    ns.DetailPen = 0;
    ns.BlockPen = 0;
    ns.ViewModes = 0;
    ns.Type = SCREENQUIET | SCREENBEHIND;
    ns.Font = (struct TextAttr*)-1;
    ns.DefaultTitle = (UBYTE*)"Attack of the PETSCII Robots";
    ns.Gadgets = 0;
    ns.CustomBitMap = 0;

    screen = OpenScreen(&ns);
    if (!screen) {
        Write(Output(), unableToInitDisplay, strlen(unableToInitDisplay));
        return;
    }

    // Get the bitmap and rastport
    screenBitMap = screen->RastPort.BitMap;

    // Allocate chunky framebuffer
    chunkyBuffer = (uint8_t*)AllocVec(SCREEN_WIDTH * SCREEN_HEIGHT, MEMF_CLEAR);
    if (!chunkyBuffer) {
        Write(Output(), notEnoughMemoryError, strlen(notEnoughMemoryError));
        return;
    }

    // Open window
    struct NewWindow nw = {0};
    nw.LeftEdge = 0;
    nw.TopEdge = 0;
    nw.Width = SCREEN_WIDTH;
    nw.Height = SCREEN_HEIGHT;
    nw.DetailPen = 0;
    nw.BlockPen = 0;
    nw.IDCMPFlags = IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_JOYSTICK;
    nw.Flags = WFLG_SIMPLE_REFRESH | WFLG_BACKDROP | WFLG_BORDERLESS | WFLG_RMBTRAP;
    nw.Screen = screen;
    nw.FirstGadget = 0;
    nw.CheckMark = 0;
    nw.Title = (UBYTE*)"";
    nw.Width = SCREEN_WIDTH;
    nw.Height = SCREEN_HEIGHT;
    nw.Type = CUSTOMSCREEN;

    window = OpenWindow(&nw);
    if (!window) {
        Write(Output(), unableToInitDisplay, strlen(unableToInitDisplay));
        return;
    }

    // Initialize lowlevel.library for joystick/gamepad with rumble support
    lowLevelBase_ = (Library*)OpenLibrary("lowlevel.library", 0);
    if (lowLevelBase_) {
        // Configure port 0 as gamepad with rumble enabled
        SetJoyPortAttrs(joystickPort_,
            JPAT_Type, JPTYPE_GAMEPAD,
            JPAT_RumbleMotor, TRUE,
            TAG_DONE);
        joystickConfigured_ = true;
    }

    // Load font data
    fontData = (uint8_t*)AllocVec(FONT_SIZE, MEMF_CLEAR);
    if (!fontData) {
        Write(Output(), notEnoughMemoryError, strlen(notEnoughMemoryError));
        return;
    }
    loadRawFile(FONT_PATH, fontData, FONT_SIZE);

    // Pre-allocate tile data pointers (they will be created in generateTiles)
    for (int i = 0; i < NUM_TILES; i++) {
        tileChunky[i] = 0;
    }

    // Allocate load buffer
    loadBufferSize_ = 200000; // Large enough for biggest asset
    loadBuffer_ = new uint8_t[loadBufferSize_];
    if (!loadBuffer_) {
        Write(Output(), notEnoughMemoryError, strlen(notEnoughMemoryError));
        return;
    }

    // Initialize audio
    initAudio();

    // Preload assets
    loadAssets();

    platform = this;
}

PlatformMorphOS::~PlatformMorphOS()
{
    // Stop audio
    stopModule();
    cleanupAudio();

    // Free chunky tile data
    for (int i = 0; i < NUM_TILES; i++) {
        FreeVec(tileChunky[i]);
    }
    FreeVec(animTileChunky);
    FreeVec(itemChunky);
    FreeVec(keyChunky);
    FreeVec(healthChunky);
    FreeVec(faceChunky);
    FreeVec(spriteChunky);
    FreeVec(tilesMask);
    delete palette;
    FreeVec(screenMemory);
    FreeVec(chunkyBuffer);
    FreeVec(fontData);
    FreeVec(moduleData);
    delete[] loadBuffer_;

    if (window) CloseWindow(window);
    if (screen) CloseScreen(screen);
    if (lowLevelBase_) CloseLibrary(lowLevelBase_);
}

// ============================================================
// File loading
// ============================================================

void PlatformMorphOS::loadRawFile(const char* filename, uint8_t* destination, uint32_t size)
{
    BPTR file = Open((STRPTR)filename, MODE_OLDFILE);
    if (file) {
        Read(file, destination, size);
        Close(file);
        return;
    }

    // Fallback: try .gz via XAD
    char gzPath[260];
    snprintf(gzPath, sizeof(gzPath), "%s.gz", filename);
    if (xadDecodeFile(gzPath, destination, size)) return;

    Write(Output(), unableToLoadData, strlen(unableToLoadData));
}

uint32_t PlatformMorphOS::loadFile(const char* filename, uint8_t* destination, uint32_t size)
{
    BPTR file = Open((STRPTR)filename, MODE_OLDFILE);
    if (file) {
        uint32_t bytesRead = Read(file, destination, size);
        Close(file);
        if (bytesRead > 0) return bytesRead;
    }

    // Fallback: try .gz via XAD
    char gzPath[260];
    snprintf(gzPath, sizeof(gzPath), "%s.gz", filename);
    if (xadDecodeFile(gzPath, destination, size)) return size;

    Write(Output(), unableToLoadData, strlen(unableToLoadData));
    return 0;
}

void PlatformMorphOS::loadAssets()
{
    // Preload images
    screenMemory = (uint8_t*)AllocVec(SCREEN_WIDTH * SCREEN_HEIGHT * PLANES / 8 + 32, MEMF_CLEAR);
    if (screenMemory) {
        loadFile(IMAGE_PATHS[0], screenMemory, SCREEN_WIDTH * SCREEN_HEIGHT * PLANES / 8 + 32);
    }

    // Preload modules
    for (int i = 0; i < 7; i++) {
        preloadedModuleData_[i] = 0;
        preloadedModuleLengths_[i] = 0;

        // Get file size
        BPTR file = Open((STRPTR)MODULE_PATHS[i], MODE_OLDFILE);
        if (file) {
            struct FileInfoBlock fib;
            if (ExamineFH(file, &fib)) {
                uint32_t len = fib.fib_Size;
                preloadedModuleData_[i] = (uint8_t*)AllocVec(len, MEMF_CLEAR);
                if (preloadedModuleData_[i]) {
                    Seek(file, 0, OFFSET_BEGINNING);
                    Read(file, preloadedModuleData_[i], len);
                    preloadedModuleLengths_[i] = len;
                }
            }
            Close(file);
        }
    }
}

// ============================================================
// Platform interface methods
// ============================================================

uint8_t* PlatformMorphOS::standardControls() const
{
    return ::standardControls;
}

void PlatformMorphOS::setInterrupt(void (*interrupt)(void))
{
    interrupt_ = interrupt;
}

void PlatformMorphOS::show()
{
    ScreenToFront(screen);
    if (window) {
        ActivateWindow(window);
    }
}

int PlatformMorphOS::framesPerSecond()
{
    struct GfxBase* GfxBase = (struct GfxBase*)OpenLibrary("graphics.library", 0);
    if (GfxBase) {
        framesPerSecond_ = (GfxBase->DisplayFlags & PAL) ? 50 : 60;
        CloseLibrary((struct Library*)GfxBase);
    }
    return framesPerSecond_;
}

uint8_t PlatformMorphOS::readKeyboard()
{
    struct IntuiMessage* msg;
    while ((msg = (struct IntuiMessage*)GetMsg(window->UserPort))) {
        uint32_t class_ = msg->Class;
        uint16_t code = msg->Code;
        uint16_t qual = msg->Qualifier;

        ReplyMsg((struct Message*)msg);

        if (class_ == IDCMP_RAWKEY) {
            bool keyDown = code < 0x80;
            uint8_t keyCode = code & 0x7f;
            uint8_t keyCodeWithShift = keyCode | shift_;

            if ((qual & IEQUALIFIER_RCOMMAND) && keyCode == 0x10) {
                quit = true;
            } else if (keyCode == 0x60 || keyCode == 0x61) {
                if (keyDown) {
                    shift_ = 0x80;
                    downKey_ |= 0x80;
                } else {
                    shift_ = 0x00;
                    if (downKey_ != 0xff) {
                        downKey_ &= 0x7f;
                    }
                }
            } else if (keyDown) {
                if (downKey_ != keyCodeWithShift && !(qual & IEQUALIFIER_REPEAT)) {
                    downKey_ = keyCodeWithShift;
                    keyToReturn_ = downKey_;
                }
            } else if (downKey_ == keyCodeWithShift) {
                downKey_ = 0xff;
            }
        }
    }

    uint8_t result = keyToReturn_;
    keyToReturn_ = 0xff;
    return result;
}

void PlatformMorphOS::keyRepeat()
{
    keyToReturn_ = downKey_;
    joystickStateToReturn_ = joystickState_;
}

void PlatformMorphOS::clearKeyBuffer()
{
    struct IntuiMessage* msg;
    while ((msg = (struct IntuiMessage*)GetMsg(window->UserPort))) {
        ReplyMsg((struct Message*)msg);
    }
    keyToReturn_ = 0xff;
    downKey_ = 0xff;
    joystickStateToReturn_ = 0;
}

bool PlatformMorphOS::isKeyOrJoystickPressed(bool gamepad)
{
    return downKey_ != 0xff || (joystickState_ != 0 &&
        ((gamepad && joystickState_ != JoystickPlay) ||
         (!gamepad && joystickState_ != JoystickBlue)));
}

uint16_t PlatformMorphOS::readJoystick(bool gamepad)
{
    uint16_t result = joystickStateToReturn_;
    joystickStateToReturn_ = 0;

    if (!joystickConfigured_) {
        return result;
    }

    // Poll joystick state via lowlevel.library
    // JPAT_ButtonState returns a ULONG with direction + button bits:
    //   JPF_BUTTON_PLAY  (0x01) - Fire/Red
    //   JPF_BUTTON_REVERSE (0x02) - Second button/Blue
    //   JPF_BUTTON_FORWARD (0x04) - Third button/Green
    //   JPF_BUTTON_EXTRA (0x08) - Fourth button/Yellow
    //   JPF_JOY_UP       (0x0100) - Up
    //   JPF_JOY_DOWN     (0x0200) - Down
    //   JPF_JOY_LEFT     (0x0400) - Left
    //   JPF_JOY_RIGHT    (0x0800) - Right
    // When gamepad=true, map d-pad/analog to direction bits.

    uint32_t rawState = GetJoyPortAttrs(joystickPort_, JPAT_ButtonState, TAG_DONE);

    if (rawState & JPF_JOY_UP)     result |= JoystickUp;
    if (rawState & JPF_JOY_DOWN)   result |= JoystickDown;
    if (rawState & JPF_JOY_LEFT)   result |= JoystickLeft;
    if (rawState & JPF_JOY_RIGHT)  result |= JoystickRight;

    if (rawState & JPF_BUTTON_PLAY)    result |= JoystickRed;
    if (rawState & JPF_BUTTON_REVERSE) result |= JoystickBlue;
    if (rawState & JPF_BUTTON_FORWARD) result |= JoystickGreen;
    if (rawState & JPF_BUTTON_EXTRA)   result |= JoystickYellow;

    // Additional gamepad buttons
    if (gamepad) {
        // On a gamepad, shoulder buttons map to Play/Reverse
        if (rawState & JPF_BUTTON_PLAY)    result |= JoystickPlay;
        if (rawState & JPF_BUTTON_REVERSE) result |= JoystickReverse;
        if (rawState & JPF_BUTTON_FORWARD) result |= JoystickForward;
        if (rawState & JPF_BUTTON_EXTRA)   result |= JoystickExtra;
    }

    // Process IDCMP_JOYSTICK messages from the Intuition window
    // (these complement the polling for event-driven button presses)
    struct IntuiMessage* msg;
    while ((msg = (struct IntuiMessage*)GetMsg(window->UserPort))) {
        if (msg->Class == IDCMP_JOYSTICK) {
            uint16_t code = msg->Code;

            if (code & 0x01) result |= JoystickUp;
            if (code & 0x02) result |= JoystickDown;
            if (code & 0x04) result |= JoystickLeft;
            if (code & 0x08) result |= JoystickRight;
            if (code & 0x10) result |= JoystickRed;
            if (code & 0x20) result |= JoystickBlue;
            if (code & 0x40) result |= JoystickGreen;
            if (code & 0x80) result |= JoystickYellow;

            if (gamepad) {
                if (code & 0x10) result |= JoystickPlay;
                if (code & 0x20) result |= JoystickReverse;
                if (code & 0x40) result |= JoystickForward;
                if (code & 0x80) result |= JoystickExtra;
            }
        }
        ReplyMsg((struct Message*)msg);
    }

    return result;
}

void PlatformMorphOS::rumble(uint8_t strength)
{
    if (!joystickConfigured_) return;

    // Set rumble motor intensity via lowlevel.library
    // strength: 0 = off, 255 = max
    SetJoyPortAttrs(joystickPort_,
        JPAT_RumbleMotor, (ULONG)strength,
        JPAT_RumbleDuration, (ULONG)-1,   // continuous until changed
        TAG_DONE);
}

void PlatformMorphOS::displayImage(Image image)
{
    if (!screenMemory) return;

    // Load the image (planar format: 320*200*4/8 = 32000 bytes + 32 bytes palette)
    uint32_t imageSize = SCREEN_WIDTH * SCREEN_HEIGHT * PLANES / 8;
    loadFile(IMAGE_PATHS[image], screenMemory, imageSize + 32);

    // Extract palette (stored after the pixel data)
    uint16_t* paletteData = (uint16_t*)(screenMemory + imageSize);
    for (int i = 0; i < 16; i++) {
        amigaPalette[i] = paletteData[i];
    }

    // Convert planar image data to chunky framebuffer
    planarToChunky(screenMemory, chunkyBuffer, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Set the screen palette via LoadRGB4
    if (screen) {
        LoadRGB4(&screen->ViewPort, amigaPalette, 16);
    }
}

void PlatformMorphOS::loadMap(Map map, uint8_t* destination)
{
    MAPNAME[17] = 'a' + map;
    loadFile(MAPNAME, destination, 8960);
}

uint8_t* PlatformMorphOS::loadTileset()
{
    // Read tileset from file
    loadRawFile(TILESET_PATH, tilesetData, 514);
    return tilesetData;
}

// ============================================================
// Tile generation
// ============================================================

void PlatformMorphOS::generateTiles(uint8_t* tileData, uint8_t* tileAttributes)
{
    // Load planar tile bitmaps
    // Tiles.raw is 4-bitplane interleaved, 32x24 pixels per tile, 253 tiles
    // File size: 97152 bytes = 32 * 24 * 4 / 8 * 253 = 384 * 253 = 97152 ✓

    uint32_t tilesFileSize = 97152;
    uint8_t* tilesPlanar = (uint8_t*)AllocVec(tilesFileSize, MEMF_CLEAR);
    if (!tilesPlanar) return;
    loadRawFile(TILES_PATH, tilesPlanar, tilesFileSize);

    // Convert each tile from planar to chunky
    int tilesPerRow = 253;
    int tileWidth = 32;
    int tileHeight = 24;
    int srcBytesPerRow = (tileWidth / 8) * PLANES; // 16 bytes per row (4 planes × 4 bytes)

    for (int t = 0; t < tilesPerRow; t++) {
        int dstTile = t;
        tileChunky[dstTile] = (uint8_t*)AllocVec(TILE_PIXEL_SIZE, MEMF_CLEAR);
        if (!tileChunky[dstTile]) continue;

        uint8_t* chunky = tileChunky[dstTile];
        uint8_t* src = tilesPlanar + t * tileHeight * srcBytesPerRow;

        for (int y = 0; y < tileHeight; y++) {
            for (int x = 0; x < tileWidth; x++) {
                uint8_t color = 0;
                for (int p = 0; p < PLANES; p++) {
                    int bit = 7 - (x & 7);
                    int byteOffset = y * srcBytesPerRow + p * (tileWidth / 8) + (x / 8);
                    if (src[byteOffset] & (1 << bit)) {
                        color |= (1 << p);
                    }
                }
                chunky[y * tileWidth + x] = color;
            }
        }
    }

    FreeVec(tilesPlanar);

    // Load animated tiles
    uint32_t animFileSize = 9216;
    uint8_t* animPlanar = (uint8_t*)AllocVec(animFileSize, MEMF_CLEAR);
    if (animPlanar) {
        loadRawFile(ANIM_TILES_PATH, animPlanar, animFileSize);
        // 17 animated tiles, same format
        int animTilesPerRow = 17;
        animTileChunky = (uint8_t*)AllocVec(animTilesPerRow * TILE_PIXEL_SIZE, MEMF_CLEAR);
        if (animTileChunky) {
            for (int t = 0; t < animTilesPerRow; t++) {
                uint8_t* chunky = animTileChunky + t * TILE_PIXEL_SIZE;
                uint8_t* src = animPlanar + t * tileHeight * srcBytesPerRow;
                for (int y = 0; y < tileHeight; y++) {
                    for (int x = 0; x < tileWidth; x++) {
                        uint8_t color = 0;
                        for (int p = 0; p < PLANES; p++) {
                            int bit = 7 - (x & 7);
                            int byteOffset = y * srcBytesPerRow + p * (tileWidth / 8) + (x / 8);
                            if (src[byteOffset] & (1 << bit)) {
                                color |= (1 << p);
                            }
                        }
                        chunky[y * tileWidth + x] = color;
                    }
                }
            }
        }
        FreeVec(animPlanar);
    }

    // Load and convert sprite data
    uint32_t spriteFileSize = 31872;
    uint8_t* spritePlanar = (uint8_t*)AllocVec(spriteFileSize, MEMF_CLEAR);
    uint8_t* spriteMaskPlanar = (uint8_t*)AllocVec(spriteFileSize, MEMF_CLEAR);
    if (spritePlanar && spriteMaskPlanar) {
        loadRawFile(SPRITES_PATH, spritePlanar, spriteFileSize);
        loadRawFile(SPRITES_MASK_PATH, spriteMaskPlanar, spriteFileSize);

        int spritesPerRow = spriteFileSize / (tileHeight * srcBytesPerRow);
        spriteChunky = (uint8_t*)AllocVec(spritesPerRow * TILE_PIXEL_SIZE, MEMF_CLEAR);
        tilesMask = (uint8_t*)AllocVec(spritesPerRow * TILE_PIXEL_SIZE, MEMF_CLEAR);
        if (spriteChunky && tilesMask) {
            for (int t = 0; t < spritesPerRow; t++) {
                uint8_t* chunky = spriteChunky + t * TILE_PIXEL_SIZE;
                uint8_t* mask = tilesMask + t * TILE_PIXEL_SIZE;
                uint8_t* src = spritePlanar + t * tileHeight * srcBytesPerRow;
                uint8_t* srcMask = spriteMaskPlanar + t * tileHeight * srcBytesPerRow;
                for (int y = 0; y < tileHeight; y++) {
                    for (int x = 0; x < tileWidth; x++) {
                        uint8_t color = 0;
                        uint8_t maskVal = 0;
                        for (int p = 0; p < PLANES; p++) {
                            int bit = 7 - (x & 7);
                            int byteOffset = y * srcBytesPerRow + p * (tileWidth / 8) + (x / 8);
                            if (src[byteOffset] & (1 << bit)) {
                                color |= (1 << p);
                            }
                            if (srcMask[byteOffset] & (1 << bit)) {
                                maskVal |= (1 << p);
                            }
                        }
                        chunky[y * tileWidth + x] = color;
                        mask[y * tileWidth + x] = maskVal;
                    }
                }
            }
        }
    }
    if (spritePlanar) FreeVec(spritePlanar);
    if (spriteMaskPlanar) FreeVec(spriteMaskPlanar);

    // Load and convert HUD elements
    // Items.raw: 4-bitplane, 48x21 per item, 6 items
    int itemW = 48, itemH = 21, itemN = 6;
    int itemBytesPerRow = (itemW / 8) * PLANES;
    uint32_t itemFileSize = itemN * itemH * itemBytesPerRow;
    uint8_t* itemPlanar = (uint8_t*)AllocVec(itemFileSize, MEMF_CLEAR);
    if (itemPlanar) {
        loadRawFile(ITEMS_PATH, itemPlanar, itemFileSize);
        itemChunky = (uint8_t*)AllocVec(itemN * itemW * itemH, MEMF_CLEAR);
        if (itemChunky) {
            for (int t = 0; t < itemN; t++) {
                uint8_t* chunky = itemChunky + t * itemW * itemH;
                uint8_t* src = itemPlanar + t * itemH * itemBytesPerRow;
                planarToChunky(src, chunky, itemW, itemH);
            }
        }
        FreeVec(itemPlanar);
    }
    itemWidth = itemW; itemHeight = itemH; itemCount = itemN;

    // Keys.raw: 4-bitplane, 16x14 per key, 3 keys
    int keyW = 16, keyH = 14, keyN = 3;
    int keyBytesPerRow = (keyW / 8) * PLANES;
    uint32_t keyFileSize = keyN * keyH * keyBytesPerRow;
    uint8_t* keyPlanar = (uint8_t*)AllocVec(keyFileSize, MEMF_CLEAR);
    if (keyPlanar) {
        loadRawFile(KEYS_PATH, keyPlanar, keyFileSize);
        keyChunky = (uint8_t*)AllocVec(keyN * keyW * keyH, MEMF_CLEAR);
        if (keyChunky) {
            for (int t = 0; t < keyN; t++) {
                uint8_t* chunky = keyChunky + t * keyW * keyH;
                uint8_t* src = keyPlanar + t * keyH * keyBytesPerRow;
                planarToChunky(src, chunky, keyW, keyH);
            }
        }
        FreeVec(keyPlanar);
    }
    keyWidth = keyW; keyHeight = keyH; keyCount = keyN;

    // Health.raw: 4-bitplane, 48x51 per health, 3 health states
    int healthW = 48, healthH = 51, healthN = 3;
    int healthBytesPerRow = (healthW / 8) * PLANES;
    uint32_t healthFileSize = healthN * healthH * healthBytesPerRow;
    uint8_t* healthPlanar = (uint8_t*)AllocVec(healthFileSize, MEMF_CLEAR);
    if (healthPlanar) {
        loadRawFile(HEALTH_PATH, healthPlanar, healthFileSize);
        healthChunky = (uint8_t*)AllocVec(healthN * healthW * healthH, MEMF_CLEAR);
        if (healthChunky) {
            for (int t = 0; t < healthN; t++) {
                uint8_t* chunky = healthChunky + t * healthW * healthH;
                uint8_t* src = healthPlanar + t * healthH * healthBytesPerRow;
                planarToChunky(src, chunky, healthW, healthH);
            }
        }
        FreeVec(healthPlanar);
    }
    healthWidth = healthW; healthHeight = healthH; healthCount = healthN;

    // Faces.raw: 4-bitplane, 16x24 per face, 3 faces
    int faceW = 16, faceH = 24, faceN = 3;
    int faceBytesPerRow = (faceW / 8) * PLANES;
    uint32_t faceFileSize = faceN * faceH * faceBytesPerRow;
    uint8_t* facePlanar = (uint8_t*)AllocVec(faceFileSize, MEMF_CLEAR);
    if (facePlanar) {
        loadRawFile(FACES_PATH, facePlanar, faceFileSize);
        faceChunky = (uint8_t*)AllocVec(faceN * faceW * faceH, MEMF_CLEAR);
        if (faceChunky) {
            for (int t = 0; t < faceN; t++) {
                uint8_t* chunky = faceChunky + t * faceW * faceH;
                uint8_t* src = facePlanar + t * faceH * faceBytesPerRow;
                planarToChunky(src, chunky, faceW, faceH);
            }
        }
        FreeVec(facePlanar);
    }
    faceWidth = faceW; faceHeight = faceH; faceCount = faceN;

    // Determine transparency for tiles
    // Tiles with sprite mappings or certain tile numbers need transparency
    for (int i = 0; i < 256; i++) {
        if (spriteTileMap[i] >= 0) {
            tileHasTransparency[i] = true;
        }
    }
    // Additional transparent tiles (explosions, bombs, etc.)
    int transparentTiles[] = {130, 134, 240, 241, 244, 245, 246, 248, 249, 250, 251, 252, -1};
    for (int i = 0; transparentTiles[i] >= 0; i++) {
        tileHasTransparency[transparentTiles[i]] = true;
    }

    // Build live map tile mapping
    static uint8_t defaultTileLiveMap[] = {
         0,13, 1, 1, 1, 1, 1, 1, 1, 5, 1, 1, 1, 1,13, 1,
         1, 1, 1, 1, 1, 1, 1, 2, 8, 1, 1, 1, 1, 6,14,14,
        15,13,14,15,15,13, 5,15, 6,13,13,12, 6,13,13,12,
         1, 1, 1,12, 1, 9, 9, 6, 1, 9,15, 6,10,10, 1, 1,
         1, 1, 7,13, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
         4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
         2, 2,13,13,13,13,13,13, 1, 1, 1,13, 4, 4, 4, 0,
         1, 1, 4,13, 4, 4, 4, 1, 1, 1, 2, 2, 2, 2, 2, 2,
         1, 1,13,10, 1, 1,13, 9, 1, 1, 4, 5,13,13,13, 4,
         1, 1, 1, 1,15, 5,15,15, 1, 1, 6, 6,15,15, 6, 5,
         2, 2, 2, 5,13,13, 1, 7, 5, 3, 1, 7, 4, 4, 4,13,
         1, 1, 1, 1, 1, 4, 4, 2, 3, 3, 3, 1, 3, 2, 3, 3,
         3, 3, 3, 1, 4, 4, 9, 9, 4, 4, 2, 2, 5,11,12,12,
         8, 8, 8, 9, 3, 3, 2,10, 1, 1, 1, 9, 1, 1, 1,10,
         1, 1, 1,13, 8, 8,13,13, 8, 8,13,13, 3, 3,13,13,
        13,13, 5,13,13,13,13,13,13,13,13,13,13,13,13,13
    };
    memcpy(tileLiveMap, defaultTileLiveMap, 256);
}

// ============================================================
// Chunky buffer rendering primitives
// ============================================================

void PlatformMorphOS::chunkyBlit(const uint8_t* source, uint16_t dx, uint16_t dy,
                                 uint16_t w, uint16_t h, bool transparent, uint8_t transparentColor)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t pixel = source[y * w + x];
            if (!transparent || pixel != transparentColor) {
                int dstX = dx + x;
                int dstY = dy + y;
                if (dstX >= 0 && dstX < SCREEN_WIDTH && dstY >= 0 && dstY < SCREEN_HEIGHT) {
                    chunkyBuffer[dstY * SCREEN_WIDTH + dstX] = pixel;
                }
            }
        }
    }
}

void PlatformMorphOS::renderTile(uint8_t tile, uint16_t x, uint16_t y, uint8_t variant, bool transparent)
{
    uint8_t* src;
    int tileW = 32, tileH = 24;

    // Determine source data
    if (transparent && spriteTileMap[tile] >= 0) {
        // Sprite-based tile
        int spriteIdx = spriteTileMap[tile] + variant;
        if (spriteChunky) {
            src = spriteChunky + spriteIdx * TILE_PIXEL_SIZE;
        } else {
            return;
        }
    } else if (animTileMap[tile] >= 0) {
        // Animated tile
        int animIdx = animTileMap[tile] + variant;
        if (animTileChunky) {
            src = animTileChunky + animIdx * TILE_PIXEL_SIZE;
        } else {
            src = tileChunky[tile];
            if (!src) return;
        }
    } else {
        src = tileChunky[tile];
        if (!src) return;
    }

    // Calculate destination in chunky buffer
    uint16_t destX = x * 8;
    uint16_t destY = y * 8;

    // Check if transparency masking is needed
    bool useTransparency = transparent || tileHasTransparency[tile];

    // Blit to chunky buffer
    for (int row = 0; row < tileH; row++) {
        for (int col = 0; col < tileW; col++) {
            uint8_t pixel = src[row * tileW + col];
            if (!useTransparency || pixel != 0) {
                int px = destX + col;
                int py = destY + row;
                if (px < SCREEN_WIDTH && py < SCREEN_HEIGHT) {
                    chunkyBuffer[py * SCREEN_WIDTH + px] = pixel;
                }
            }
        }
    }
}

void PlatformMorphOS::renderTiles(uint8_t backgroundTile, uint8_t foregroundTile,
                                  uint16_t x, uint16_t y,
                                  uint8_t backgroundVariant, uint8_t foregroundVariant)
{
    // Render background tile first
    renderTile(backgroundTile, x, y, backgroundVariant, false);

    // Render foreground tile with transparency
    if (spriteTileMap[foregroundTile] >= 0) {
        int spriteIdx = spriteTileMap[foregroundTile] + foregroundVariant;
        if (spriteChunky) {
            uint16_t destX = x * 8;
            uint16_t destY = y * 8;
            uint8_t* src = spriteChunky + spriteIdx * TILE_PIXEL_SIZE;
            for (int row = 0; row < 24; row++) {
                for (int col = 0; col < 32; col++) {
                    uint8_t pixel = src[row * 32 + col];
                    if (pixel != 0) {
                        int px = destX + col;
                        int py = destY + row;
                        if (px < SCREEN_WIDTH && py < SCREEN_HEIGHT) {
                            chunkyBuffer[py * SCREEN_WIDTH + px] = pixel;
                        }
                    }
                }
            }
        }
    } else {
        renderTile(foregroundTile, x, y, foregroundVariant, true);
    }
}

void PlatformMorphOS::renderItem(uint8_t item, uint16_t x, uint16_t y)
{
    if (!itemChunky || item >= itemCount) return;
    uint8_t* src = itemChunky + item * itemWidth * itemHeight;
    chunkyBlit(src, x, y, itemWidth, itemHeight, true, 0);
}

void PlatformMorphOS::renderKey(uint8_t key, uint16_t x, uint16_t y)
{
    if (!keyChunky || key >= keyCount) return;
    uint8_t* src = keyChunky + key * keyWidth * keyHeight;
    chunkyBlit(src, x, y, keyWidth, keyHeight, true, 0);
}

void PlatformMorphOS::renderHealth(uint8_t health, uint16_t x, uint16_t y)
{
    if (!healthChunky || health >= healthCount) return;
    uint8_t* src = healthChunky + health * healthWidth * healthHeight;
    chunkyBlit(src, x, y, healthWidth, healthHeight, true, 0);
}

void PlatformMorphOS::renderFace(uint8_t face, uint16_t x, uint16_t y)
{
    if (!faceChunky || face >= faceCount) return;
    uint8_t* src = faceChunky + face * faceWidth * faceHeight;
    chunkyBlit(src, x, y, faceWidth, faceHeight, true, 0);
}

// ============================================================
// Live map
// ============================================================

void PlatformMorphOS::renderLiveMap(uint8_t* map)
{
    // Clear map area
    clearRect(0, 0, SCREEN_WIDTH - 56, SCREEN_HEIGHT - 32);

    // Draw the map: 128x64 tiles, each tile is 2x2 pixels
    for (int my = 0; my < 64; my++) {
        for (int mx = 0; mx < 128; mx++) {
            uint8_t tile = tileLiveMap[map[my * 128 + mx]];
            uint8_t color = tile & 0x0f;
            int px = mx * 2;
            int py = 20 + my * 2;
            chunkyBuffer[py * SCREEN_WIDTH + px] = color;
            chunkyBuffer[py * SCREEN_WIDTH + px + 1] = color;
            chunkyBuffer[(py + 1) * SCREEN_WIDTH + px] = color;
            chunkyBuffer[(py + 1) * SCREEN_WIDTH + px + 1] = color;
        }
    }
}

void PlatformMorphOS::renderLiveMapTile(uint8_t* map, uint8_t mx, uint8_t my)
{
    uint8_t tile = tileLiveMap[map[(my << 7) + mx]];
    uint8_t color = tile & 0x0f;
    int px = mx * 2;
    int py = 20 + my * 2;
    chunkyBuffer[py * SCREEN_WIDTH + px] = color;
    chunkyBuffer[py * SCREEN_WIDTH + px + 1] = color;
    chunkyBuffer[(py + 1) * SCREEN_WIDTH + px] = color;
    chunkyBuffer[(py + 1) * SCREEN_WIDTH + px + 1] = color;
}

void PlatformMorphOS::renderLiveMapUnits(uint8_t* map, uint8_t* unitTypes,
                                          uint8_t* unitX, uint8_t* unitY,
                                          uint8_t playerColor, bool showRobots)
{
    // Render unit dots on live map
    for (int i = 0; i < 48; i++) {
        if (i == 0 || (unitTypes[i] != 255 && unitTypes[i] != 0)) {
            if (i == 0 || showRobots || unitTypes[i] == 22) {
                int mx = unitX[i];
                int my = unitY[i];
                int px = mx * 2;
                int py = 20 + my * 2;
                uint8_t color = (i == 0) ? playerColor : 15;
                chunkyBuffer[py * SCREEN_WIDTH + px] = color;
                chunkyBuffer[py * SCREEN_WIDTH + px + 1] = color;
                chunkyBuffer[(py + 1) * SCREEN_WIDTH + px] = color;
                chunkyBuffer[(py + 1) * SCREEN_WIDTH + px + 1] = color;
            }
        }
    }
}

// ============================================================
// Cursor
// ============================================================

void PlatformMorphOS::showCursor(uint16_t x, uint16_t y)
{
    cursorX_ = x;
    cursorY_ = y;
    cursorVisible_ = true;

    // Draw a simple crosshair cursor
    int px = x * 8 + 8;
    int py = y * 8 + 8;
    uint8_t color = 15; // white

    for (int i = -1; i <= 1; i++) {
        if (px + i >= 0 && px + i < SCREEN_WIDTH && py >= 0 && py < SCREEN_HEIGHT)
            chunkyBuffer[py * SCREEN_WIDTH + px + i] = color;
        if (px >= 0 && px < SCREEN_WIDTH && py + i >= 0 && py + i < SCREEN_HEIGHT)
            chunkyBuffer[(py + i) * SCREEN_WIDTH + px] = color;
    }
}

void PlatformMorphOS::hideCursor()
{
    cursorVisible_ = false;
}

void PlatformMorphOS::setCursorShape(CursorShape shape)
{
    cursorShape_ = shape;
}

// ============================================================
// Rect operations
// ============================================================

void PlatformMorphOS::copyRect(uint16_t sourceX, uint16_t sourceY,
                                uint16_t destinationX, uint16_t destinationY,
                                uint16_t width, uint16_t height)
{
    // Simple blit within chunky buffer
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sx = sourceX + x;
            int sy = sourceY + y;
            int dx = destinationX + x;
            int dy = destinationY + y;
            if (sx < SCREEN_WIDTH && sy < SCREEN_HEIGHT &&
                dx < SCREEN_WIDTH && dy < SCREEN_HEIGHT) {
                chunkyBuffer[dy * SCREEN_WIDTH + dx] = chunkyBuffer[sy * SCREEN_WIDTH + sx];
            }
        }
    }
}

void PlatformMorphOS::clearRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    for (int row = 0; row < height && (y + row) < SCREEN_HEIGHT; row++) {
        for (int col = 0; col < width && (x + col) < SCREEN_WIDTH; col++) {
            chunkyBuffer[(y + row) * SCREEN_WIDTH + (x + col)] = 0;
        }
    }
}

void PlatformMorphOS::fillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t color)
{
    for (int row = 0; row < height && (y + row) < SCREEN_HEIGHT; row++) {
        for (int col = 0; col < width && (x + col) < SCREEN_WIDTH; col++) {
            chunkyBuffer[(y + row) * SCREEN_WIDTH + (x + col)] = color;
        }
    }
}

// ============================================================
// Screen shake
// ============================================================

void PlatformMorphOS::startShakeScreen()
{
    shakeStep_ = 0;
    shakeOffsetX_ = 0;
}

void PlatformMorphOS::shakeScreen()
{
    shakeStep_++;
    if (shakeStep_ > 4) {
        shakeStep_ = 1;
    }
    if (shakeStep_ < 3) {
        shakeOffsetX_ = 4;
    } else {
        shakeOffsetX_ = -4;
    }
}

void PlatformMorphOS::stopShakeScreen()
{
    shakeStep_ = 0;
    shakeOffsetX_ = 0;
}

// ============================================================
// Fade effects
// ============================================================

void PlatformMorphOS::startFadeScreen(uint16_t color, uint16_t intensity)
{
    if (!palette) return;
    palette->setFadeBaseColor(color);
    palette->setFade(intensity);
    if (screen) {
        LoadRGB4(&screen->ViewPort, palette->palette(), 16);
    }
}

void PlatformMorphOS::fadeScreen(uint16_t intensity, bool immediate)
{
    if (!palette) return;
    uint16_t fade = palette->fade();
    if (fade != intensity) {
        if (immediate) {
            palette->setFade(intensity);
            if (screen) {
                LoadRGB4(&screen->ViewPort, palette->palette(), 16);
            }
        } else {
            int16_t fadeDelta = intensity > fade ? 1 : -1;
            do {
                fade += fadeDelta;
                palette->setFade(fade);
                if (screen) {
                    LoadRGB4(&screen->ViewPort, palette->palette(), 16);
                }
                WaitTOF();
            } while (fade != intensity);
        }
    }
}

void PlatformMorphOS::stopFadeScreen()
{
    if (!palette) return;
    palette->setFade(15);
    if (screen) {
        LoadRGB4(&screen->ViewPort, palette->palette(), 16);
    }
}

// ============================================================
// Screen memory access (character-based text output)
// ============================================================

void PlatformMorphOS::writeToScreenMemory(address_t address, uint8_t value)
{
    writeToScreenMemory(address, value, 10, 0);
}

void PlatformMorphOS::writeToScreenMemory(address_t address, uint8_t value,
                                           uint8_t color, uint8_t yOffset)
{
    if (!fontData) return;

    bool reverse = value > 127;
    uint8_t charIdx = value & 127;
    uint8_t* glyph = fontData + charIdx * 8;

    uint16_t startX = addressMap[address] % SCREEN_WIDTH;
    uint16_t startY = addressMap[address] / SCREEN_WIDTH + yOffset;

    for (int row = 0; row < 8 && (startY + row) < SCREEN_HEIGHT; row++) {
        uint8_t fontByte = reverse ? ~glyph[row] : glyph[row];
        for (int col = 0; col < 8 && (startX + col) < SCREEN_WIDTH; col++) {
            if (fontByte & (1 << (7 - col))) {
                chunkyBuffer[(startY + row) * SCREEN_WIDTH + (startX + col)] = color;
            } else {
                chunkyBuffer[(startY + row) * SCREEN_WIDTH + (startX + col)] = 0;
            }
        }
    }
}

// ============================================================
// Audio via Reggae multimedia.class BOOPSI API
// ============================================================
//
// Reggae API summary:
//   MultimediaBase = OpenLibrary("multimedia/multimedia.class", 52);
//   AudioOutputBase = OpenLibrary("multimedia/audio.output", 51);
//   media = MediaNewObjectTags(MMA_StreamType,"file.stream", MMA_StreamName,path, MMA_MediaType,MMT_SOUND, TAG_END);
//   play = NewObject(NULL, "audio.output", TAG_END);
//   MediaConnectTagList(media, 0, play, 0, NULL);
//   DoMethod(play, MMM_Play);
//   DoMethod(play, MMM_Stop);
//   DoMethod(play, MMM_Pause);
//   DisposeObject(play);
//   DisposeObject(media);
//   CloseLibrary(AudioOutputBase);
//   CloseLibrary(MultimediaBase);

// ============================================================
// WAV header for wrapping raw PCM samples
// ============================================================
// Standard 44-byte RIFF/WAV header for 8-bit unsigned mono PCM
#define WAV_HEADER_SIZE 44
#define SFX_SAMPLE_RATE 8287

static void makeWavHeader(uint8_t* header, uint32_t dataSize)
{
    uint32_t fileSize = dataSize + 36; // chunk sizes after RIFF header
    uint32_t byteRate = SFX_SAMPLE_RATE;

    header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
    header[4] = fileSize & 0xff;
    header[5] = (fileSize >> 8) & 0xff;
    header[6] = (fileSize >> 16) & 0xff;
    header[7] = (fileSize >> 24) & 0xff;
    header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
    header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
    header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0; // chunk size = 16
    header[20] = 1; header[21] = 0; // PCM format
    header[22] = 1; header[23] = 0; // mono
    header[24] = SFX_SAMPLE_RATE & 0xff;
    header[25] = (SFX_SAMPLE_RATE >> 8) & 0xff;
    header[26] = (SFX_SAMPLE_RATE >> 16) & 0xff;
    header[27] = (SFX_SAMPLE_RATE >> 24) & 0xff;
    header[28] = byteRate & 0xff;
    header[29] = (byteRate >> 8) & 0xff;
    header[30] = (byteRate >> 16) & 0xff;
    header[31] = (byteRate >> 24) & 0xff;
    header[32] = 1; header[33] = 0; // block align
    header[34] = 8; header[35] = 0; // bits per sample
    header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
    header[40] = dataSize & 0xff;
    header[41] = (dataSize >> 8) & 0xff;
    header[42] = (dataSize >> 16) & 0xff;
    header[43] = (dataSize >> 24) & 0xff;
}

void PlatformMorphOS::loadSamples()
{
    for (int i = 0; i < NUM_SAMPLES; i++) {
        sampleCache[i] = 0;
        sampleSizes[i] = 0;
        sampleLoaded[i] = false;

        BPTR file = Open((STRPTR)SFX_PATHS[i], MODE_OLDFILE);
        if (file) {
            struct FileInfoBlock fib;
            if (ExamineFH(file, &fib)) {
                uint32_t rawLen = fib.fib_Size;
                uint32_t wavLen = rawLen + WAV_HEADER_SIZE;
                uint8_t* buf = (uint8_t*)AllocVec(wavLen, MEMF_CLEAR);
                if (buf) {
                    makeWavHeader(buf, rawLen);
                    if ((uint32_t)Read(file, buf + WAV_HEADER_SIZE, rawLen) == rawLen) {
                        sampleCache[i] = buf;
                        sampleSizes[i] = wavLen;
                        sampleLoaded[i] = true;
                    } else {
                        FreeVec(buf);
                    }
                }
            }
            Close(file);
        }
    }
}

void PlatformMorphOS::initAudio()
{
    // Init Reggae for module music
    MultimediaBase = OpenLibrary("multimedia/multimedia.class", 52);
    AudioOutputBase = OpenLibrary("multimedia/audio.output", 51);
    if (!MultimediaBase || !AudioOutputBase) {
        if (MultimediaBase) { CloseLibrary(MultimediaBase); MultimediaBase = 0; }
        if (AudioOutputBase) { CloseLibrary(AudioOutputBase); AudioOutputBase = 0; }
        Write(Output(), "multimedia.class or audio.output not found, audio disabled\n", 64);
        return;
    }

    // Allocate module data buffer for largest module
    moduleData = (uint8_t*)AllocVec(200000, MEMF_CLEAR);
    sampleOutput = 0;

    // Pre-load and wrap all sample files as WAV in cache
    loadSamples();

    audioInitialized_ = true;
}

void PlatformMorphOS::cleanupAudio()
{
    stopModule();
    stopSample();

    for (int i = 0; i < NUM_SAMPLES; i++) {
        FreeVec(sampleCache[i]);
        sampleCache[i] = 0;
    }

    if (AudioOutputBase) { CloseLibrary(AudioOutputBase); AudioOutputBase = 0; }
    if (MultimediaBase) { CloseLibrary(MultimediaBase); MultimediaBase = 0; }
    audioInitialized_ = false;
}

void PlatformMorphOS::playNote(uint8_t note)
{
}

void PlatformMorphOS::stopNote()
{
}

void PlatformMorphOS::loadModule(Module module)
{
    if (module == loadedModule || !moduleData) return;

    int idx = (module == ModuleSoundFX) ? 7 : (module - 1);

    if (preloadedModuleData_[idx]) {
        uint32_t len = preloadedModuleLengths_[idx];
        memcpy(moduleData, preloadedModuleData_[idx], len);
    } else {
        loadRawFile(MODULE_PATHS[idx], moduleData, 200000);
    }

    loadedModule = module;
}

void PlatformMorphOS::playModule(Module module)
{
    if (!MultimediaBase || !AudioOutputBase) return;

    stopModule();

    int idx = (module == ModuleSoundFX) ? 7 : (module - 1);

    moduleMedia = (Object*)MediaNewObjectTags(
        MMA_StreamType, (ULONG)"file.stream",
        MMA_StreamName, (ULONG)MODULE_PATHS[idx],
        MMA_MediaType, MMT_SOUND,
    TAG_END);

    if (!moduleMedia) return;

    moduleOutput = (Object*)NewObject(NULL, "audio.output", TAG_END);
    if (!moduleOutput) {
        DisposeObject((Object*)moduleMedia);
        moduleMedia = 0;
        return;
    }

    MediaConnectTagList((Object*)moduleMedia, 0, (Object*)moduleOutput, 0, NULL);
    DoMethod((Object*)moduleOutput, MMM_Play);
}

void PlatformMorphOS::pauseModule()
{
    if (moduleOutput) {
        DoMethod((Object*)moduleOutput, MMM_Pause);
    }
}

void PlatformMorphOS::stopModule()
{
    if (moduleOutput) {
        DoMethod((Object*)moduleOutput, MMM_Stop);
        DisposeObject((Object*)moduleOutput);
        moduleOutput = 0;
    }
    if (moduleMedia) {
        DisposeObject((Object*)moduleMedia);
        moduleMedia = 0;
    }
}

void PlatformMorphOS::playSample(uint8_t sample)
{
    if (!audioInitialized_ || sample >= NUM_SAMPLES || !sampleLoaded[sample])
        return;

    // Stop any previous sample
    stopSample();

    // Trigger rumble on explosion (index 0 = sounds_dsbarexp.raw)
    if (sample == 0) {
        rumble(255);
    }

    uint8_t* data = sampleCache[sample];
    QUAD length = sampleSizes[sample];

    // Create Reggae memory stream + audio.output pipeline
    Object* media = (Object*)MediaNewObjectTags(
        MMA_StreamType, (ULONG)"memory.stream",
        MMA_StreamHandle, (ULONG)data,
        MMA_StreamLength, (IPTR)&length,
        MMA_MediaType, MMT_SOUND,
    TAG_END);

    if (!media) return;

    Object* output = (Object*)NewObject(NULL, "audio.output", TAG_END);
    if (!output) {
        DisposeObject(media);
        return;
    }

    MediaConnectTagList(media, 0, output, 0, NULL);
    DoMethod(output, MMM_Play);

    sampleMedia = media;
    sampleOutput = output;
}

void PlatformMorphOS::stopSample()
{
    if (sampleOutput) {
        DoMethod((Object*)sampleOutput, MMM_Stop);
        DisposeObject((Object*)sampleOutput);
        sampleOutput = 0;
    }
    if (sampleMedia) {
        DisposeObject((Object*)sampleMedia);
        sampleMedia = 0;
    }
}

// ============================================================
// Frame rendering
// ============================================================

void PlatformMorphOS::renderFrame(bool waitForNextFrame)
{
    if (!screen || !chunkyBuffer) return;

    // Wait for VBL if requested
    if (waitForNextFrame) {
        WaitTOF();
    }

    // Apply shake offset
    int renderOffsetX = shakeOffsetX_;

    // Write chunky buffer to screen using WriteChunkyPixels (cybergraphics.library)
    // This is the most efficient way to write chunky pixel data to an RTG bitmap
    // WriteChunkyPixels(RastPort*, x, y, width, height, array, bytesPerRow)
    int srcX = (renderOffsetX < 0) ? -renderOffsetX : 0;
    int dstX = (renderOffsetX > 0) ? renderOffsetX : 0;
    int width = SCREEN_WIDTH - ((renderOffsetX < 0) ? -renderOffsetX : renderOffsetX);
    if (width > 0) {
        WriteChunkyPixels(&screen->RastPort, dstX, 0, width, SCREEN_HEIGHT,
                          (UBYTE *)(chunkyBuffer + srcX), SCREEN_WIDTH);
    }

    // Execute interrupt callback if set
    if (interrupt_) {
        interrupt_();
    }

    frameCount_++;
}

void PlatformMorphOS::waitForScreenMemoryAccess()
{
    // No blitter to wait for in RTG mode
}

void PlatformMorphOS::setHighlightedMenuRow(uint16_t row)
{
    // Could be used for menu highlighting effects
}

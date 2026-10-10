#include <Arduino.h>
#include <Preferences.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "lilka.h"
#include "doom_splash.h"
#include "doom_presentation.h"
#include "wad_picker.h"
#include "display_settings.h"
#include <lilka/volume_overlay.h>

extern "C" {
#include "i_sound.h"
#include "doomkeys.h"
#include "doomgeneric.h"
#include "d_alloc.h"
#include "d_main.h"
#include "doomstat.h"
#include "i_video.h"
#include "i_system.h"
#include "st_stuff.h"
}

extern void doomgeneric_Create(int argc, char** argv);
extern void doomgeneric_Tick();

typedef struct {
    uint8_t key;
    bool pressed;
} doomkey_t;

doomkey_t keyqueue[16];
uint16_t keyqueueRead = 0;
uint16_t keyqueueWrite = 0;

SemaphoreHandle_t inputMutex;
SemaphoreHandle_t backBufferMutex;
EventGroupHandle_t backBufferEvent;
TaskHandle_t gameTaskHandle;
TaskHandle_t drawTaskHandle;

uint16_t* backBuffer = NULL;
bool frameUiMode = false;
bool frameWipeActive = false;
extern "C" boolean32 inhelpscreens;

// Three fixed DOS-style lines, well inside the rounded display corners.
// Only the status line changes, at most once every 400 ms during startup.
constexpr int bootColumns = 38;
constexpr uint32_t bootRefreshMs = 400;
char bootCurrent[bootColumns + 1] = {};
char bootPending[bootColumns + 1] = {};
char bootVisible[bootColumns + 1] = {};
int bootColumn = 0;
uint32_t bootLastPaint = 0;
bool bootPendingReady = false;
bool bootConsoleActive = false;

bool bootHasLetters(const char* text) {
    for (; *text; ++text) {
        if ((*text >= 'A' && *text <= 'Z') || (*text >= 'a' && *text <= 'z')) return true;
    }
    return false;
}

void drawBootStatus(const char* text) {
    if (strcmp(text, bootVisible) == 0) return;
    lilka::display.fillRect(24, 108, lilka::display.width() - 48,
                            16, lilka::colors::Black);
    lilka::display.setCursor(24, 120);
    lilka::display.print(text);
    strncpy(bootVisible, text, bootColumns);
    bootVisible[bootColumns] = '\0';
    bootLastPaint = millis();
}

void writeBootText(const char* text) {
    if (!bootConsoleActive) return;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        const unsigned char c = *p;
        if (c == '\n') {
            if (bootHasLetters(bootCurrent)) {
                strcpy(bootPending, bootCurrent);
                bootPendingReady = true;
            }
            bootColumn = 0;
            bootCurrent[0] = '\0';
        } else if (c == '\r') {
            bootColumn = 0;
            bootCurrent[0] = '\0';
        } else if (c == '\b') {
            if (bootColumn > 0) bootCurrent[--bootColumn] = '\0';
        } else if ((c == '\t' || (c >= 32 && c < 127)) && bootColumn < bootColumns) {
            bootCurrent[bootColumn++] = c == '\t' ? ' ' : c;
            bootCurrent[bootColumn] = '\0';
        }
    }

    if (millis() - bootLastPaint >= bootRefreshMs) {
        if (bootPendingReady) {
            drawBootStatus(bootPending);
            bootPendingReady = false;
        } else if (bootHasLetters(bootCurrent)) {
            drawBootStatus(bootCurrent);
        }
    }
}

void startBootConsole(const char* wadPath) {
    bootColumn = 0;
    bootCurrent[0] = bootPending[0] = bootVisible[0] = '\0';
    bootPendingReady = false;
    lilka::display.fillScreen(lilka::colors::Black);
    lilka::display.setFont(FONT_8x13_MONO);
    lilka::display.setTextColor(lilka::display.color565(255, 185, 65));
    lilka::display.setCursor(24, 68);
    lilka::display.print("DOOM / LILKA");
    lilka::display.setFont(FONT_6x12);
    lilka::display.setTextColor(lilka::colors::White);
    const char* basename = strrchr(wadPath, '/');
    char wadLine[bootColumns + 1];
    snprintf(wadLine, sizeof(wadLine), "IWAD: %.30s", basename ? basename + 1 : wadPath);
    lilka::display.setCursor(24, 92);
    lilka::display.print(wadLine);
    drawBootStatus("INITIALIZING ENGINE...");
    bootLastPaint = millis();
    bootConsoleActive = true;
}

sound_module_t DG_sound_module;
extern sound_module_t sound_module_I2S;
extern sound_module_t sound_module_Buzzer;
extern sound_module_t sound_module_NoSound;
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

void gameTask(void* arg);
void drawTask(void* arg);

extern "C" void restartAfterDoomQuit() {
    lilka::sys.restart();
}

char nextWeaponKey = '2';

void buttonHandler(lilka::Button button, bool pressed) {
    // Select is exclusively the SDK shortcut modifier, including cancellation
    // callbacks/releases. Start already opens AND confirms in M_Responder;
    // A/B retain the engine's existing menu confirm/back interpretation.
    if (button == lilka::Button::SELECT) return;
    xSemaphoreTake(inputMutex, portMAX_DELAY);
    doomkey_t* key = &keyqueue[keyqueueWrite];
    switch (button) {
        case lilka::Button::UP:
            key->key = KEY_UPARROW;
            break;
        case lilka::Button::DOWN:
            key->key = KEY_DOWNARROW;
            break;
        case lilka::Button::LEFT:
            key->key = KEY_LEFTARROW;
            break;
        case lilka::Button::RIGHT:
            key->key = KEY_RIGHTARROW;
            break;
        // No strafing
        case lilka::Button::A:
            key->key = KEY_FIRE;
            break;
        case lilka::Button::B:
            key->key = KEY_USE;
            break;
        case lilka::Button::C:
            key->key = KEY_TAB;
            break;
        case lilka::Button::D:
            // Cycle weapons
            key->key = nextWeaponKey;
            break;
        // Strafing experiment
        // case lilka::Button::A:
        //     key->key = KEY_STRAFE_R;
        //     break;
        // case lilka::Button::B:
        //     key->key = KEY_FIRE;
        //     break;
        // case lilka::Button::C:
        //     key->key = KEY_USE;
        //     break;
        // case lilka::Button::D:
        //     key->key = KEY_STRAFE_L;
        //     break;
        case lilka::Button::START:
            key->key = KEY_ENTER;
            break;
        default:
            // TODO: Log warning?
            xSemaphoreGive(inputMutex);
            return;
    }

    key->pressed = pressed;
    keyqueueWrite = (keyqueueWrite + 1) % 16;
    xSemaphoreGive(inputMutex);
}

bool ensureSdDirectory(const String& path) {
    if (!SD.exists(path.c_str()) && !SD.mkdir(path.c_str())) return false;
    File directory = SD.open(path.c_str());
    const bool valid = directory && directory.isDirectory();
    directory.close();
    return valid;
}

// ESP-IDF stack budgets are bytes. Preserve the Doom engine budget.
constexpr uint32_t gameStackBytes = 32768;
// Maps + scanline = 2240 bytes; overlay has one small u8g2 decoder.
// TFT/SPI streaming uses scalars/register buffers, not recursive GFX drawing.
// Leave generous call/RTOS headroom without a second engine-size stack.
constexpr uint32_t drawStackBytes = 16384;

void releaseStartupResources() {
    // Both tasks stay at their notification gate until engine startup succeeds.
    if (gameTaskHandle) { vTaskDelete(gameTaskHandle); gameTaskHandle = nullptr; }
    if (drawTaskHandle) { vTaskDelete(drawTaskHandle); drawTaskHandle = nullptr; }
    free(backBuffer); backBuffer = nullptr;
    free(DG_ScreenBuffer); DG_ScreenBuffer = nullptr;
    if (backBufferEvent) { vEventGroupDelete(backBufferEvent); backBufferEvent = nullptr; }
    if (backBufferMutex) { vSemaphoreDelete(backBufferMutex); backBufferMutex = nullptr; }
    if (inputMutex) { vSemaphoreDelete(inputMutex); inputMutex = nullptr; }
}

const char* prepareStartupResources() {
    inputMutex = xSemaphoreCreateMutex();
    backBufferMutex = xSemaphoreCreateMutex();
    backBufferEvent = xEventGroupCreate();
    if (!inputMutex || !backBufferMutex || !backBufferEvent) return "Sync allocation failed";
    // Reserve both PSRAM frames before any engine callback can swap/draw them.
    const size_t frameBytes = DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(*backBuffer);
    backBuffer = static_cast<uint16_t*>(ps_malloc(frameBytes));
    DG_ScreenBuffer = static_cast<uint16_t*>(ps_malloc(frameBytes));
    if (!backBuffer || !DG_ScreenBuffer) return "Framebuffer allocation failed";
    const auto drawResult = xTaskCreatePinnedToCore(drawTask, "drawTask", drawStackBytes,
                                                  nullptr, 1, &drawTaskHandle, 1);
    if (drawResult != pdPASS) return "Renderer task allocation failed";
    const auto gameResult = xTaskCreatePinnedToCore(gameTask, "gameTask", gameStackBytes,
                                                  nullptr, 1, &gameTaskHandle, 0);
    if (gameResult != pdPASS) return "Game task allocation failed";
    return nullptr;
}

void waitForEngineStart() {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

void startDoomTasks() {
    bootConsoleActive = false;
    lilka::controller.setGlobalHandler(buttonHandler);
    // Only called after BOTH creations and engine initialization succeed.
    xTaskNotifyGive(drawTaskHandle);
    xTaskNotifyGive(gameTaskHandle);
}

void startupFailure(const char* reason) {
    releaseStartupResources();
    bootConsoleActive = false;
    lilka::serial_log("Doom startup FAILED: %s", reason);
    lilka::display.fillScreen(lilka::colors::Black);
    lilka::display.setFont(FONT_6x12);
    lilka::display.setTextColor(lilka::colors::White);
    lilka::display.setCursor(24, 92);
    lilka::display.print("DOOM STARTUP FAILED");
    lilka::display.setCursor(24, 120);
    lilka::display.print(reason);
    lilka::display.setCursor(24, 148);
    lilka::display.print("See serial log. Reboot manually.");
    // Do not reset-loop or launch an audio-only game.
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}

void initializeDoomRuntime(int argc, char** argv) {
    const char* failure = prepareStartupResources();
    if (failure) startupFailure(failure);
    if (!D_TryAllocBuffers()) startupFailure("Engine buffer allocation failed");
    // Register before Doom does so this callback runs after its own cleanup.
    I_AtExit(restartAfterDoomQuit, false);
    doomgeneric_Create(argc, argv);
    startDoomTasks();
}

void setup() {
    lilka::display.setSplash(doom_splash);
    lilka::begin();
    lilka::displaySettings.begin();

    int argc = 3;
    char arg[] = "doomgeneric";
    char arg2[] = "-iwad";
    char arg3[256];

    // The SDK's getFirmwarePath() removes this key, which also removes Doom
    // from Keira's Applications menu after returning from the guest.
    String firmwareFile;
    Preferences prefs;
    if (prefs.begin("lilka", true)) {
        firmwareFile = prefs.getString("multiboot_path", "");
        prefs.end();
    }
    lilka::serial_log("Firmware file: %s", firmwareFile.c_str());
    String firmwareDir = "/";
    if (firmwareFile.length()) {
        // Get directory from firmware file
        int lastSlash = firmwareFile.lastIndexOf('/');
        if (lastSlash > 0) firmwareDir = firmwareFile.substring(0, lastSlash);
    }

    lilka::display.fillScreen(lilka::colors::Black);
    lilka::display.setFont(FONT_6x12);
    lilka::display.setTextColor(lilka::colors::White);
    lilka::display.setCursor(24, 120);
    lilka::display.print("Scanning WAD files...");

    String selectedWadName;
    const WadPickResult pickResult = pickWad(firmwareDir, selectedWadName);
    const String selectedWadPath = lilka::fileutils.getSDRoot() + firmwareDir
                                 + (firmwareDir.endsWith("/") ? "" : "/") + selectedWadName;
    if (pickResult != WadPickResult::Selected || selectedWadPath.length() >= sizeof(arg3)) {
        const char* reason = pickResult == WadPickResult::DirectoryUnavailable
                           ? "Папка WAD недоступна"
                           : pickResult == WadPickResult::TooMany
                           ? "Забагато WAD-файлів (максимум 64)"
                           : pickResult == WadPickResult::NoneFound
                           ? "Не знайдено сумісних Doom IWAD"
                           : "Шлях до WAD занадто довгий";
        lilka::Alert alert("Doom", reason);
        alert.draw(&lilka::display);
        while (!alert.isFinished()) {
            alert.update();
        }
        lilka::sys.restart();
    }
    memcpy(arg3, selectedWadPath.c_str(), selectedWadPath.length() + 1);
    lilka::serial_log("Selected IWAD: %s\n", arg3);

    String saveRoot = firmwareDir;
    if (!saveRoot.endsWith("/")) saveRoot += "/";
    saveRoot += "saves";
    const String selectedSaveDir = saveRoot + "/" + selectedWadName;
    if (!ensureSdDirectory(saveRoot) || !ensureSdDirectory(selectedSaveDir)) {
        lilka::serial_log("Cannot create save directory: %s\n", selectedSaveDir.c_str());
        lilka::Alert alert("Doom", "Не вдалося створити папку збережень");
        alert.draw(&lilka::display);
        while (!alert.isFinished()) alert.update();
        lilka::sys.restart();
    }
    char* argv[3] = {arg, arg2, arg3};

    int soundDevice = -1;
    lilka::Canvas canvas;
    while (soundDevice < 0) {
        lilka::Menu soundMenu("Звуковий пристрій");
        soundMenu.addItem("I2S DAC");
        soundMenu.addItem("П'єзо-динамік");
        soundMenu.addItem("Без звуку");
        while (!soundMenu.isFinished()) {
            if (doomDisplay::serviceStartupIdle()) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            soundMenu.update();
            soundMenu.draw(&canvas);
            lilka::display.drawCanvas(&canvas);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        soundDevice = soundMenu.getCursor();
    }
    // Restore selected brightness before engine startup; no idle sleep/dim in game.
    lilka::displaySettings.serviceIdle(false);

    if (soundDevice == 0) {
        // I2S DAC
        DG_sound_module = sound_module_I2S;
    } else if (soundDevice == 1) {
        // Buzzer
        DG_sound_module = sound_module_Buzzer;
    } else {
        // No sound
        DG_sound_module = sound_module_NoSound;
    }

    startBootConsole(arg3);

    DG_printf("Doomgeneric starting, WAD file: %s", arg3);

    initializeDoomRuntime(argc, argv);

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    // D_FreeBuffers(); // TODO - never reached
}

void gameTask(void* arg) {
    waitForEngineStart();
    while (1) {
        doomgeneric_Tick();

        if (playeringame[consoleplayer]) {
            // We have a player (TODO: might be demo)
            const player_t* plyr = &players[consoleplayer];
            const weapontype_t weapons[NUMWEAPONS] = {
                wp_fist,
                wp_chainsaw,
                wp_pistol,
                wp_shotgun,
                wp_supershotgun,
                wp_chaingun,
                wp_missile,
                wp_plasma,
                wp_bfg,
            };
            const int weaponKeys[NUMWEAPONS] = {'1', '1', '2', '3', '3', '4', '5', '6', '7'};
            int currentWeaponIndex;
            for (int i = 0; i < NUMWEAPONS; i++) {
                if (plyr->readyweapon == weapons[i]) {
                    currentWeaponIndex = i;
                    break;
                }
            }
            nextWeaponKey = weaponKeys[plyr->readyweapon];
            for (int i = 1; i < NUMWEAPONS; i++) {
                int candidate = (currentWeaponIndex + i) % NUMWEAPONS;
                if (plyr->weaponowned[weapons[candidate]] && plyr->ammo[weaponinfo[weapons[candidate]].ammo]) {
                    nextWeaponKey = weaponKeys[candidate];
                    break;
                }
            }
        }

        taskYIELD();
    }
}

void drawTask(void* arg) {
    waitForEngineStart();
    const int outputWidth = lilka::display.width();
    const int outputHeight = lilka::display.height();
    const DoomPresentation presentation(outputWidth, outputHeight);
    bool frameReady = false;
    bool previousOverlayVisible = false;
    while (1) {
        // Only this owner touches the LCD. Poll at most every 50 ms so a
        // paused/static retained frame can present adjustment and expiry too.
        const auto ready = xEventGroupWaitBits(backBufferEvent, 1, pdTRUE, pdTRUE,
                                               pdMS_TO_TICKS(50));
        const uint32_t now = millis();
        auto overlay = lilka::audio.getVolumeOverlay();
        const auto light = lilka::brightness.getOverlay();
        if (light.visible(now) && (!overlay.visible(now) || now - light.adjustedAt < now - overlay.adjustedAt)) {
            overlay = light;
        }
        const bool overlayVisible = overlay.visible(now);
        frameReady = frameReady || (ready & 1);
        if (!frameReady || (!(ready & 1) && !overlayVisible && !previousOverlayVisible)) continue;
        previousOverlayVisible = overlayVisible;
        xSemaphoreTake(backBufferMutex, portMAX_DELAY);

        const bool uiMode = frameUiMode;
        const bool wipeActive = frameWipeActive;

        lilka::display.startWrite();
        uint16_t row[DOOMGENERIC_RESX];
        const auto g = lilka::volumeOverlayGeometry(outputWidth, outputHeight);
        auto writeRow = [&](int physicalY) {
            // No decoder work outside the 76-row panel. All primitives are
            // RAM-only; the LCD receives each pixel once, already final.
            if (overlayVisible && g.width && physicalY >= g.y && physicalY < g.y + g.height) {
                lilka::VolumeOverlayRow target{row, 0, physicalY, outputWidth};
                lilka::drawVolumeOverlay(target, overlay, outputWidth, outputHeight, now);
            }
            lilka::display.writePixels(row, outputWidth);
        };
        // Wipe endpoints have already been physically composed in indexed
        // color by DG_ComposeWipeFrame. Never remap them as world-only frames.
        lilka::display.writeAddrWindow(0, 0, outputWidth, outputHeight);
        for (int y = 0; y < outputHeight; ++y) {
            if (wipeActive) {
                for (int x = 0; x < outputWidth; ++x) row[x] = backBuffer[y * outputWidth + x];
            } else {
                presentation.row(backBuffer, uiMode, y, row, ST_HudBackground565);
            }
            writeRow(y);
        }
        lilka::display.endWrite();

        xSemaphoreGive(backBufferMutex);
        taskYIELD();
    }
}

extern "C" void DG_Init() {
}

extern "C" void DG_ComposeWipeFrame(const uint8_t* source, uint8_t* destination, int endFrame, int* width, int* height) {
    xSemaphoreTake(backBufferMutex, portMAX_DELAY);
    const bool uiMode = endFrame ? (gamestate != GS_LEVEL || inhelpscreens) : frameUiMode;
    const DoomPresentation presentation(lilka::display.width(), lilka::display.height());
    *width = presentation.width;
    *height = presentation.height;
    // Pack the physical panel into the existing 320x240 allocation (both
    // rotations fit). Melt uses this pitch/height, not native engine rows.
    // Clear unused tail bytes too; no extra framebuffer is allocated.
    // Consecutive state changes can start another wipe before any native
    // redraw. That previous endpoint is already physical: do not map twice.
    if (!endFrame && frameWipeActive) {
        std::memcpy(destination, source, DOOMGENERIC_RESX * DOOMGENERIC_RESY);
        xSemaphoreGive(backBufferMutex);
        return;
    }
    std::memset(destination, 0, DOOMGENERIC_RESX * DOOMGENERIC_RESY);
    for (int y = 0; y < presentation.height; ++y) {
        presentation.row(source, uiMode, y, destination + y * presentation.width, ST_HudBackgroundIndex);
    }
    xSemaphoreGive(backBufferMutex);
}

extern "C" void DG_DrawFrame() {
    // Frame is ready.
    // Acquire back buffer, swap buffers and set event
    xSemaphoreTake(backBufferMutex, portMAX_DELAY);
    uint16_t* temp = backBuffer;
    backBuffer = DG_ScreenBuffer;
    DG_ScreenBuffer = temp;
    frameUiMode = gamestate != GS_LEVEL || inhelpscreens;
    frameWipeActive = D_WipeInProgress();
    xEventGroupSetBits(backBufferEvent, 1);
    xSemaphoreGive(backBufferMutex);
}

extern "C" void DG_SetWindowTitle(const char* title) {
    Serial.print("DG: window title: ");
    Serial.println(title);
}

extern "C" void DG_SleepMs(uint32_t ms) {
    delay(ms);
}

extern "C" uint32_t DG_GetTicksMs() {
    return millis();
}

extern "C" int DG_GetKey(int* pressed, unsigned char* doomKey) {
    xSemaphoreTake(inputMutex, portMAX_DELAY);
    int ret;
    if (keyqueueRead != keyqueueWrite) {
        const doomkey_t* key = &keyqueue[keyqueueRead];
        *pressed = key->pressed;
        *doomKey = key->key;
        keyqueueRead = (keyqueueRead + 1) % 16;
        ret = 1;
    } else {
        ret = 0;
    }
    xSemaphoreGive(inputMutex);
    return ret;
}

extern "C" void DG_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    printf("[DG log] ");
    vprintf(format, args);
    va_end(args);

    if (bootConsoleActive) {
        char message[192];
        va_start(args, format);
        vsnprintf(message, sizeof(message), format, args);
        va_end(args);
        writeBootText(message);
    }
}

void loop() {
}

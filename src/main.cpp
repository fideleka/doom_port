#include <Arduino.h>
#include <Preferences.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include "lilka.h"
#include "doom_splash.h"
#include "wad_picker.h"
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

void logStartupHeap(const char* stage) {
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    lilka::serial_log("Doom startup %s: internal free=%u largest=%u PSRAM free=%u largest=%u",
        stage, static_cast<unsigned>(heap_caps_get_free_size(internal)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(internal)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

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
    logStartupHeap("before tasks");
    const auto drawResult = xTaskCreatePinnedToCore(drawTask, "drawTask", drawStackBytes,
                                                  nullptr, 1, &drawTaskHandle, 1);
    lilka::serial_log("Doom startup drawTask: result=%ld stack=%u", static_cast<long>(drawResult),
                     static_cast<unsigned>(drawStackBytes));
    if (drawResult != pdPASS) return "Renderer task allocation failed";
    const auto gameResult = xTaskCreatePinnedToCore(gameTask, "gameTask", gameStackBytes,
                                                  nullptr, 1, &gameTaskHandle, 0);
    lilka::serial_log("Doom startup gameTask: result=%ld stack=%u", static_cast<long>(gameResult),
                     static_cast<unsigned>(gameStackBytes));
    if (gameResult != pdPASS) return "Game task allocation failed";
    logStartupHeap("tasks reserved");
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
    logStartupHeap("FAILED before cleanup");
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
    logStartupHeap("engine initialized");
    startDoomTasks();
}

void setup() {
    lilka::display.setSplash(doom_splash);
    lilka::begin();

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

    // Select sound device
    lilka::Menu soundMenu("Звуковий пристрій");
    soundMenu.addItem("I2S DAC");
    soundMenu.addItem("П'єзо-динамік");
    soundMenu.addItem("Без звуку");
    lilka::Canvas canvas;
    while (!soundMenu.isFinished()) {
        soundMenu.update();
        soundMenu.draw(&canvas);
        lilka::display.drawCanvas(&canvas);
    }
    int soundDevice = soundMenu.getCursor();

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
    // Preserve Doom's native status art from x=0..250. The rightmost table
    // starts at x=251; never rescale the retained AMMO/HEALTH/ARMS/face/
    // ARMOR/keys strip to fill the panel.
    const int statusSourceWidth = 251;
    const int statusWidth = statusSourceWidth * outputWidth / 280;
    const int statusX = (outputWidth - statusWidth) / 2;
    const int statusHeight = 32 * outputWidth / 280;
    const int ammoHeight = 10 * outputWidth / 280;
    const int uiHeight = outputWidth * 3 / 4;
    const int uiY = (outputHeight - uiHeight) / 2;
    // Build the horizontal coordinate maps once, not in every pixel of every
    // frame. The status map also exchanges the original ARMS and face strips.
    uint8_t statusSourceX[DOOMGENERIC_RESX] = {};
    uint16_t uiSourceX[DOOMGENERIC_RESX] = {};
    uint16_t worldSourceX[DOOMGENERIC_RESX] = {};
    for (int x = 0; x < outputWidth; ++x) {
        uiSourceX[x] = x * DOOMGENERIC_RESX / outputWidth;
        worldSourceX[x] = 20 + x * 280 / outputWidth;
    }
    for (int x = statusX; x < statusX + statusWidth; ++x) {
        int sourceX = (x - statusX) * statusSourceWidth / statusWidth;
        if (sourceX >= 104 && sourceX < 139) {
            sourceX += 39; // Face source x=143..177 -> destination x=104..138.
        } else if (sourceX >= 139 && sourceX < 178) {
            sourceX -= 35; // ARMS source x=104..142 -> destination x=139..177.
        }
        statusSourceX[x] = sourceX;
    }
    bool firstFramePresented = false;
    bool frameReady = false;
    bool previousOverlayVisible = false;
    while (1) {
        // Only this owner touches the LCD. Poll at most every 50 ms so a
        // paused/static retained frame can present adjustment and expiry too.
        const auto ready = xEventGroupWaitBits(backBufferEvent, 1, pdTRUE, pdTRUE,
                                               pdMS_TO_TICKS(50));
        const uint32_t now = millis();
        const auto overlay = lilka::audio.getVolumeOverlay();
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
        if (uiMode) {
            // Include letterbox rows in the same final-pixel transaction:
            // rotation can put the centered panel across a letterbox edge.
            lilka::display.writeAddrWindow(0, 0, outputWidth, outputHeight);
            for (int y = 0; y < outputHeight; y++) {
                if (y < uiY || y >= uiY + uiHeight) {
                    for (int x = 0; x < outputWidth; x++) row[x] = lilka::colors::Black;
                } else {
                    const int sourceY = (y - uiY) * SCREENHEIGHT_UI / uiHeight;
                    for (int x = 0; x < outputWidth; x++) {
                        const int sourceX = uiSourceX[x];
                        row[x] = backBuffer[sourceY * DOOMGENERIC_RESX + sourceX];
                    }
                }
                writeRow(y);
            }
        } else if (wipeActive) {
            // Doom's melt is already composited in the source framebuffer.
            // Show only that moving image while it runs; no static HUD panel
            // should float above black/moving wipe columns.
            lilka::display.writeAddrWindow(0, 0, outputWidth, outputHeight);
            for (int y = 0; y < outputHeight; y++) {
                const int sourceY = y * 208 / outputHeight;
                for (int x = 0; x < outputWidth; x++) {
                    const int sourceX = worldSourceX[x];
                    row[x] = backBuffer[sourceY * DOOMGENERIC_RESX + sourceX];
                }
                writeRow(y);
            }
        } else {
            // The classic cropped HUD is the only stable GS_LEVEL layout,
            // including in-game menus and automap.
            // Anchor the original status art to the physical bottom edge.
            // Its labels and keys remain inside the rounded-corner safe area.
            const int worldHeight = outputHeight - ammoHeight - statusHeight;

            // Keep the 280-column world framing even when a menu overlays a
            // running level; only title/help artwork uses the 4:3 mapping.
            lilka::display.writeAddrWindow(0, 0, outputWidth, worldHeight);
            for (int y = 0; y < worldHeight; y++) {
                const int sourceY = y * 208 / worldHeight;
                for (int x = 0; x < outputWidth; x++) {
                    const int sourceX = worldSourceX[x];
                    row[x] = backBuffer[sourceY * DOOMGENERIC_RESX + sourceX];
                }
                writeRow(y);
            }

            // Each original table row is exactly six pixels high. Two
            // original STBAR bevel rows above/below frame its labels and
            // current/max numbers without leaking adjacent rows.
            lilka::display.writeAddrWindow(0, worldHeight, outputWidth, ammoHeight);
            for (int y = 0; y < ammoHeight; y++) {
                for (int x = 0; x < outputWidth; x++) {
                    const int ammoType = x * 4 / outputWidth;
                    const int withinCell = x - ammoType * outputWidth / 4;
                    const int cellWidth = outputWidth / 4;
                    const int sourceX = 250 + withinCell * 70 / cellWidth;
                    if (y < 2 || y >= ammoHeight - 2) {
                        const int stoneY = y < 2 ? y : 32 - (ammoHeight - y);
                        row[x] = ST_HudBackground565(sourceX, stoneY);
                    } else if (withinCell < 2 || (ammoType == 3 && withinCell >= cellWidth - 2)) {
                        // The original key/table divider is a two-pixel
                        // light-and-dark edge, repeated for each cell.
                        row[x] = ST_HudBackground565(249 + withinCell % 2,
                                                      y * 31 / (ammoHeight - 1));
                    } else {
                        const int sourceY = 213 + ammoType * 6
                                          + (y - 2) * 6 / (ammoHeight - 4);
                        row[x] = backBuffer[sourceY * DOOMGENERIC_RESX + sourceX];
                    }
                }
                writeRow(worldHeight + y);
            }

            // Always center the same native classic crop in GS_LEVEL.
            lilka::display.writeAddrWindow(0, worldHeight + ammoHeight,
                                         outputWidth, statusHeight);
            for (int y = 0; y < statusHeight; y++) {
                const int sourceY = 208 + y * 32 / statusHeight;
                for (int x = 0; x < outputWidth; x++) {
                    if (x < statusX) {
                        // Match the clean right end cap; x=292..305 contains
                        // the carved slash ornament Anton does not want here.
                        row[x] = ST_HudBackground565(305 + x * 14 / statusX, sourceY - 208);
                        continue;
                    } else if (x >= statusX + statusWidth) {
                        const int rightWidth = outputWidth - statusX - statusWidth;
                        row[x] = ST_HudBackground565(305 + (x - statusX - statusWidth) * 15 / rightWidth,
                                                      sourceY - 208);
                        continue;
                    }
                    const int sourceX = statusSourceX[x];
                    row[x] = backBuffer[sourceY * DOOMGENERIC_RESX + sourceX];
                }
                writeRow(worldHeight + ammoHeight + y);
            }
        }
        lilka::display.endWrite();

        xSemaphoreGive(backBufferMutex);
        if (!firstFramePresented) {
            firstFramePresented = true;
            lilka::serial_log("Doom startup: first frame presented");
        }
        taskYIELD();
    }
}

extern "C" void DG_Init() {
}

extern "C" void DG_DrawFrame() {
    // Frame is ready.
    // Acquire back buffer, swap buffers and set event
    xSemaphoreTake(backBufferMutex, portMAX_DELAY);
    uint16_t* temp = backBuffer;
    backBuffer = DG_ScreenBuffer;
    DG_ScreenBuffer = temp;
    frameUiMode = gamestate != GS_LEVEL || inhelpscreens;
    frameWipeActive = !frameUiMode && D_WipeInProgress();
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

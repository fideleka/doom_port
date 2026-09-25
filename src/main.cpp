#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "lilka.h"
#include "doom_splash.h"
#include "wad_picker.h"

extern "C" {
#include "i_sound.h"
#include "doomkeys.h"
#include "doomgeneric.h"
#include "d_alloc.h"
#include "doomstat.h"
#include "i_video.h"
#include "i_system.h"
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

uint32_t* backBuffer = NULL;
bool frameUiMode = false;
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
    esp_restart();
}

char nextWeaponKey = '2';

void buttonHandler(lilka::Button button, bool pressed) {
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
        case lilka::Button::SELECT:
            key->key = KEY_ESCAPE;
            break;
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

void setup() {
    lilka::display.setSplash(doom_splash);
    lilka::begin();

    inputMutex = xSemaphoreCreateMutex();
    xSemaphoreGive(inputMutex);
    backBufferMutex = xSemaphoreCreateMutex();
    xSemaphoreGive(backBufferMutex);
    backBufferEvent = xEventGroupCreate();
    xEventGroupClearBits(backBufferEvent, 1);

    int argc = 3;
    char arg[] = "doomgeneric";
    char arg2[] = "-iwad";
    char arg3[256];

    // Get firmware arg
    String firmwareFile = lilka::multiboot.getFirmwarePath();
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
        esp_restart();
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
        esp_restart();
        return;
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

    D_AllocBuffers();
    // Back buffer must be allocated before doomgeneric_Create since it calls DG_DrawFrame
    backBuffer = static_cast<uint32_t*>(malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4));
    // Register before Doom does so this callback runs after its own cleanup.
    I_AtExit(restartAfterDoomQuit, false);
    doomgeneric_Create(argc, argv);
    if (backBuffer == NULL) {
        DG_printf("Failed to allocate back buffer\n");
        esp_restart();
    }
    bootConsoleActive = false;

    lilka::controller.setGlobalHandler(buttonHandler);

    xTaskCreatePinnedToCore(gameTask, "gameTask", 32768, NULL, 1, &gameTaskHandle, 0);
    xTaskCreatePinnedToCore(drawTask, "drawTask", 32768, NULL, 1, &drawTaskHandle, 1);

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    // D_FreeBuffers(); // TODO - never reached
}

void gameTask(void* arg) {
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
    const int outputWidth = lilka::display.width();
    const int outputHeight = lilka::display.height();
    // Keep all status-bar data inside the panel's rounded bottom corners.
    const int statusSide = outputWidth * 12 / 280;
    const int statusBottom = outputWidth * 8 / 280;
    const int statusWidth = outputWidth - 2 * statusSide;
    const int statusHeight = 32 * statusWidth / DOOMGENERIC_RESX;
    const int worldHeight = outputHeight - statusHeight - statusBottom;
    const int uiHeight = outputWidth * 3 / 4;
    const int uiY = (outputHeight - uiHeight) / 2;
    bool previousUiMode = false;

    while (1) {
        // Wait for buffer to be ready
        xEventGroupWaitBits(backBufferEvent, 1, pdTRUE, pdTRUE, portMAX_DELAY);
        xSemaphoreTake(backBufferMutex, portMAX_DELAY);

        const bool uiMode = frameUiMode;
        if (uiMode != previousUiMode) {
            lilka::display.fillScreen(lilka::colors::Black);
        }
        previousUiMode = uiMode;

        lilka::display.startWrite();
        uint16_t row[DOOMGENERIC_RESX];
        if (uiMode) {
            lilka::display.writeAddrWindow(0, uiY, outputWidth, uiHeight);
            for (int y = 0; y < uiHeight; y++) {
                const int sourceY = y * SCREENHEIGHT_UI / uiHeight;
                for (int x = 0; x < outputWidth; x++) {
                    const int sourceX = x * DOOMGENERIC_RESX / outputWidth;
                    const uint32_t pixel = backBuffer[sourceY * DOOMGENERIC_RESX + sourceX];
                    row[x] = lilka::display.color565((pixel >> 16) & 0xff,
                                                    (pixel >> 8) & 0xff,
                                                    pixel & 0xff);
                }
                lilka::display.writePixels(row, outputWidth);
            }
        } else {
            // Keep the 280-column world framing even when a menu overlays a
            // running level; only title/help artwork uses the 4:3 mapping.
            lilka::display.writeAddrWindow(0, 0, outputWidth, worldHeight);
            for (int y = 0; y < worldHeight; y++) {
                const int sourceY = y * 208 / worldHeight;
                for (int x = 0; x < outputWidth; x++) {
                    const int sourceX = 20 + x * 280 / outputWidth;
                    const uint32_t pixel = backBuffer[sourceY * DOOMGENERIC_RESX + sourceX];
                    row[x] = lilka::display.color565((pixel >> 16) & 0xff,
                                                    (pixel >> 8) & 0xff,
                                                    pixel & 0xff);
                }
                lilka::display.writePixels(row, outputWidth);
            }

            // Extend the panel's own edge texels into its safety margins.
            // The informative center stays inset from the rounded corners.
            lilka::display.writeAddrWindow(0, worldHeight,
                                         outputWidth, statusHeight);
            for (int y = 0; y < statusHeight; y++) {
                const int sourceY = 208 + y * 32 / statusHeight;
                for (int x = 0; x < outputWidth; x++) {
                    const int sourceX = x < statusSide ? 0
                                      : x >= statusSide + statusWidth ? DOOMGENERIC_RESX - 1
                                      : (x - statusSide) * DOOMGENERIC_RESX / statusWidth;
                    const uint32_t pixel = backBuffer[sourceY * DOOMGENERIC_RESX + sourceX];
                    row[x] = lilka::display.color565((pixel >> 16) & 0xff,
                                                    (pixel >> 8) & 0xff,
                                                    pixel & 0xff);
                }
                lilka::display.writePixels(row, outputWidth);
            }
        }
        lilka::display.endWrite();

        xSemaphoreGive(backBufferMutex);
        taskYIELD();
    }
}

extern "C" void DG_Init() {
}

extern "C" void DG_DrawFrame() {
    // Frame is ready.
    // Acquire back buffer, swap buffers and set event
    xSemaphoreTake(backBufferMutex, portMAX_DELAY);
    uint32_t* temp = backBuffer;
    backBuffer = DG_ScreenBuffer;
    DG_ScreenBuffer = temp;
    frameUiMode = gamestate != GS_LEVEL || automapactive || inhelpscreens;
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

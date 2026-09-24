#include <Arduino.h>
#include "lilka.h"
#include "doom_splash.h"

extern "C" {
#include "i_sound.h"
#include "doomkeys.h"
#include "doomgeneric.h"
#include "d_alloc.h"
#include "doomstat.h"
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
volatile uint32_t diagnosticGameFrames = 0;
volatile uint32_t diagnosticDrawFrames = 0;
volatile uint32_t diagnosticKeyEvents = 0;
volatile uint8_t diagnosticGamePhase = 0;
volatile uint8_t diagnosticDrawPhase = 0;
extern "C" volatile int doomDisplayPhase;

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
    diagnosticKeyEvents++;
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
    char arg3[64];

    // Get firmware arg
    String firmwareFile = lilka::multiboot.getFirmwarePath();
    lilka::serial_log("Firmware file: %s", firmwareFile.c_str());
    String firmwareDir;
    if (firmwareFile.length()) {
        // Get directory from firmware file
        int lastSlash = firmwareFile.lastIndexOf('/');
        firmwareDir = firmwareFile.substring(0, lastSlash);
        if (firmwareDir.length() == 0) {
            firmwareDir = "/";
        }
    } else {
        firmwareDir = "/";
    }

    bool found = false;
    // Find the WAD file
    File root = SD.open(firmwareDir.c_str());
    File file;
    while ((file = root.openNextFile())) {
        if (file.isDirectory()) {
            file.close();
            continue;
        }
        String name(file.name());
        name.toLowerCase();
        lilka::serial_log("Checking file: %s", name.c_str());
        if (name.startsWith("doom") && name.endsWith(".wad")) {
            if (firmwareDir.endsWith("/")) {
                firmwareDir = firmwareDir.substring(0, firmwareDir.length() - 1);
            }
            strcpy(arg3, (lilka::fileutils.getSDRoot() + firmwareDir + "/" + file.name()).c_str());
            lilka::serial_log("Found .WAD file: %s\n", arg3);
            found = true;
            file.close();
            break;
        }
        file.close();
    }
    root.close();
    if (!found) {
        lilka::Alert alert("Doom", "Не знайдено .WAD-файлу на картці пам'яті");
        alert.draw(&lilka::display);
        while (!alert.isFinished()) {
            alert.update();
        }
        esp_restart();
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

    lilka::display.fillScreen(lilka::colors::Black);

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

    lilka::controller.setGlobalHandler(buttonHandler);

    // while (1) {
    //     doomgeneric_Tick();
    // }

    Serial.println("Ready, starting tasks");

    xTaskCreatePinnedToCore(gameTask, "gameTask", 32768, NULL, 1, &gameTaskHandle, 0);
    xTaskCreatePinnedToCore(drawTask, "drawTask", 32768, NULL, 1, &drawTaskHandle, 1);

    while (1) {
        Serial.printf("DOOM diag: game=%lu draw=%lu keys=%lu gamePhase=%u displayPhase=%d drawPhase=%u menu=%d state=%d heap=%u stackGame=%u stackDraw=%u\n",
                      (unsigned long)diagnosticGameFrames,
                      (unsigned long)diagnosticDrawFrames,
                      (unsigned long)diagnosticKeyEvents,
                      (unsigned)diagnosticGamePhase,
                      doomDisplayPhase,
                      (unsigned)diagnosticDrawPhase,
                      (int)menuactive,
                      (int)gamestate,
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned)uxTaskGetStackHighWaterMark(gameTaskHandle),
                      (unsigned)uxTaskGetStackHighWaterMark(drawTaskHandle));
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    // D_FreeBuffers(); // TODO - never reached
}

void gameTask(void* arg) {
    while (1) {
        diagnosticGamePhase = 1;
        doomgeneric_Tick();
        diagnosticGameFrames++;
        diagnosticGamePhase = 2;

        // Print free memory
        // Serial.print("Free heap: ");
        // Serial.print(ESP.getFreeHeap());

        // Print free stack
        // Serial.print("  |  Game task free stack: ");
        // Serial.println(uxTaskGetStackHighWaterMark(NULL));

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
            // Print player position
            // Serial.printf(
            //     "Player health: %d, armor: %d, ammo: %d\r\n",
            //     plyr->health,
            //     plyr->armorpoints,
            //     plyr->ammo[weaponinfo[plyr->readyweapon].ammo]
            // );
            // if (plyr->mo) {
            //     Serial.printf("Player position: %d, %d, %d\r\n", plyr->mo->x, plyr->mo->y, plyr->mo->z);
            // }
        }

        taskYIELD();
    }
}

void drawTask(void* arg) {
    const int outputWidth = lilka::display.width();
    const int outputHeight = lilka::display.height();
    const int statusHeight = 32 * outputWidth / DOOMGENERIC_RESX;
    const int worldHeight = outputHeight - statusHeight;
    const int uiHeight = outputWidth * 3 / 4;
    const int uiY = (outputHeight - uiHeight) / 2;
    bool previousUiMode = false;

    while (1) {
        diagnosticDrawPhase = 1;
        // Wait for buffer to be ready
        xEventGroupWaitBits(backBufferEvent, 1, pdTRUE, pdTRUE, portMAX_DELAY);
        diagnosticDrawPhase = 2;
        xSemaphoreTake(backBufferMutex, portMAX_DELAY);
        diagnosticDrawPhase = 3;

        const bool uiMode = frameUiMode;
        if (uiMode && !previousUiMode) {
            lilka::display.fillScreen(lilka::colors::Black);
        }
        previousUiMode = uiMode;

        lilka::display.startWrite();
        diagnosticDrawPhase = 4;
        lilka::display.writeAddrWindow(0, uiMode ? uiY : 0, outputWidth,
                                     uiMode ? uiHeight : outputHeight);
        uint16_t row[DOOMGENERIC_RESX];
        for (int y = 0; y < (uiMode ? uiHeight : outputHeight); y++) {
            const bool statusRow = !uiMode && y >= worldHeight;
            const int sourceY = uiMode ? y * 200 / uiHeight
                              : statusRow ? 208 + (y - worldHeight) * 32 / statusHeight
                                          : y * 208 / worldHeight;
            for (int x = 0; x < outputWidth; x++) {
                const int sourceX = uiMode || statusRow
                                  ? x * DOOMGENERIC_RESX / outputWidth
                                  : 20 + x * 280 / outputWidth;
                uint32_t pixel = backBuffer[sourceY * DOOMGENERIC_RESX + sourceX];
                uint8_t r = (pixel >> 16) & 0xff;
                uint8_t g = (pixel >> 8) & 0xff;
                uint8_t b = pixel & 0xff;
                row[x] = lilka::display.color565(r, g, b);
            }
            lilka::display.writePixels(row, outputWidth);
        }
        lilka::display.endWrite();
        diagnosticDrawPhase = 5;

        xSemaphoreGive(backBufferMutex);
        diagnosticDrawFrames++;
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
    frameUiMode = menuactive || gamestate != GS_LEVEL || automapactive;
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
    // Keep engine diagnostics on serial; never draw over the game viewport.
    va_list args;
    va_start(args, format);
    printf("[DG log] ");
    vprintf(format, args);
    va_end(args);
}

void loop() {
}

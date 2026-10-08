#pragma once
#include "lilka.h"

namespace doomDisplay {
inline String timeoutText(uint32_t seconds) {
    if (!seconds) return "Never";
    return seconds % 60 == 0 ? String(seconds / 60) + " min" : String(seconds) + " s";
}

inline void changeTimeout(int direction, bool dim) {
    const uint32_t choices[] = {0, 30, 60, 120, 300, 600};
    const uint32_t current = dim ? lilka::displaySettings.getDimTimeoutSeconds() :
                                  lilka::displaySettings.getTimeoutSeconds();
    uint32_t next = current;
    for (auto value : choices) {
        if (direction > 0 && value > current) { next = value; break; }
        if (direction < 0 && value < current) next = value;
    }
    if (dim) lilka::displaySettings.setDimTimeoutSeconds(next);
    else lilka::displaySettings.setTimeoutSeconds(next);
}

// Startup menus have one LCD owner. Gameplay never runs the idle policy.
inline bool serviceStartupIdle() {
    lilka::displaySettings.serviceIdle(true);
    return lilka::displaySettings.isSleeping();
}

inline void showSettings() {
    lilka::Menu menu("Display");
    menu.addItem("Brightness");
    menu.addItem("Auto-off");
    menu.addItem("Idle dim");
    menu.addActivationButton(lilka::Button::B);
    menu.removeActivationButton(lilka::Button::A);
    menu.setHorizontalNavigationEnabled(false);
    lilka::Canvas canvas;
    while (!menu.isFinished()) {
        if (serviceStartupIdle()) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        menu.setItem(0, "Brightness", nullptr, lilka::colors::White,
            lilka::brightness.isEnabled() ? String("< ") + String(lilka::brightness.getBrightness()) + "% >" : String("Unavailable"));
        menu.setItem(1, "Auto-off", nullptr, lilka::colors::White,
            lilka::displaySettings.isAvailable() ? String("< ") + timeoutText(lilka::displaySettings.getTimeoutSeconds()) + " >" : String("Unavailable"));
        menu.setItem(2, "Idle dim", nullptr, lilka::colors::White,
            lilka::displaySettings.isAvailable() && lilka::brightness.isEnabled() ?
                String("< ") + timeoutText(lilka::displaySettings.getDimTimeoutSeconds()) + " >" : String("Unavailable"));
        const auto state = lilka::controller.peekState();
        if (!state.selectHeld) {
            const bool less = state.left.justPressed || state.d.justPressed;
            const bool more = state.right.justPressed || state.a.justPressed;
            if (less != more) {
                const int direction = more ? 1 : -1;
                if (menu.getCursor() == 0) lilka::brightness.stepBrightnessShortcut(direction);
                else if (menu.getCursor() == 1) changeTimeout(direction, false);
                else if (lilka::brightness.isEnabled()) changeTimeout(direction, true);
            }
        }
        menu.update();
        menu.draw(&canvas);
        lilka::display.drawCanvas(&canvas);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
} // namespace doomDisplay

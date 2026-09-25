#pragma once

#include <Arduino.h>

enum class WadPickResult {
    Selected,
    DirectoryUnavailable,
    NoneFound,
    TooMany,
};

// Select a playable Doom-family IWAD from the firmware's SD directory.
WadPickResult pickWad(const String& directory, String& selectedName);

#include "wad_picker.h"

#include <SD.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <strings.h>
#include <vector>

#include "lilka.h"

namespace {

constexpr size_t maxWads = 64;
constexpr uint32_t maxLumps = 100000;
constexpr size_t wadEntrySize = 16;
constexpr size_t entriesPerRead = 32;

uint32_t readLE32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0])
         | static_cast<uint32_t>(data[1]) << 8
         | static_cast<uint32_t>(data[2]) << 16
         | static_cast<uint32_t>(data[3]) << 24;
}

bool lumpNameIs(const uint8_t* actual, const char* expected) {
    for (size_t i = 0; i < 8; ++i) {
        if (expected[i] == '\0') {
            for (; i < 8; ++i) {
                if (actual[i] != '\0' && actual[i] != ' ') return false;
            }
            return true;
        }
        if (std::toupper(actual[i]) != expected[i]) return false;
    }
    return expected[8] == '\0';
}

bool isPlayableDoomIwad(File& file) {
    uint8_t header[12];
    if (file.size() < sizeof(header) || !file.seek(0)
        || file.read(header, sizeof(header)) != sizeof(header)
        || memcmp(header, "IWAD", 4) != 0) {
        return false;
    }

    const uint32_t lumpCount = readLE32(header + 4);
    const uint32_t directoryOffset = readLE32(header + 8);
    const uint64_t directoryEnd = static_cast<uint64_t>(directoryOffset)
                                + static_cast<uint64_t>(lumpCount) * wadEntrySize;
    if (lumpCount == 0 || lumpCount > maxLumps || directoryOffset < sizeof(header)
        || directoryEnd > file.size() || !file.seek(directoryOffset)) {
        return false;
    }

    bool hasLevel = false;
    bool hasPalette = false;
    bool hasColormap = false;
    bool hasMenu = false;
    bool hasStatusBar = false;
    uint8_t entries[entriesPerRead * wadEntrySize];
    for (uint32_t remaining = lumpCount; remaining > 0;) {
        const size_t count = std::min<size_t>(remaining, entriesPerRead);
        const size_t bytes = count * wadEntrySize;
        if (file.read(entries, bytes) != bytes) return false;
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* name = entries + i * wadEntrySize + 8;
            if (!hasLevel) hasLevel = lumpNameIs(name, "E1M1") || lumpNameIs(name, "MAP01");
            if (!hasPalette) hasPalette = lumpNameIs(name, "PLAYPAL");
            if (!hasColormap) hasColormap = lumpNameIs(name, "COLORMAP");
            if (!hasMenu) hasMenu = lumpNameIs(name, "M_DOOM");
            if (!hasStatusBar) hasStatusBar = lumpNameIs(name, "STBAR");
        }
        if (hasLevel && hasPalette && hasColormap && hasMenu && hasStatusBar) return true;
        remaining -= count;
    }
    return false;
}

}  // namespace

WadPickResult pickWad(const String& directory, String& selectedName) {
    File root = SD.open(directory.c_str());
    if (!root || !root.isDirectory()) return WadPickResult::DirectoryUnavailable;

    std::vector<String> wadNames;
    File file;
    while ((file = root.openNextFile())) {
        if (!file.isDirectory()) {
            String name = file.name();
            const int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            String lowercase = name;
            lowercase.toLowerCase();
            if (lowercase.endsWith(".wad") && isPlayableDoomIwad(file)) {
                if (wadNames.size() == maxWads) {
                    file.close();
                    root.close();
                    return WadPickResult::TooMany;
                }
                wadNames.push_back(name);
            }
        }
        file.close();
    }
    root.close();
    if (wadNames.empty()) return WadPickResult::NoneFound;

    std::sort(wadNames.begin(), wadNames.end(), [](const String& a, const String& b) {
        return strcasecmp(a.c_str(), b.c_str()) < 0;
    });

    lilka::Menu menu("Виберіть WAD");
    for (const String& name : wadNames) menu.addItem(name);
    lilka::Canvas canvas;
    while (!menu.isFinished()) {
        menu.update();
        menu.draw(&canvas);
        lilka::display.drawCanvas(&canvas);
    }
    selectedName = wadNames[menu.getCursor()];
    return WadPickResult::Selected;
}

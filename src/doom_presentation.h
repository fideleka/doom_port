#pragma once
#include <cstdint>

// One source -> physical composition for regular frames AND wipe endpoints.
// Melt operates after this mapping, including all HUD bevels/end caps.
struct DoomPresentation {
    int width, height;
    int statusSourceWidth, statusWidth, statusX, statusHeight, ammoHeight, uiHeight, uiY;
    uint8_t statusSourceX[320] = {};
    uint16_t uiSourceX[320] = {}, worldSourceX[320] = {};
    DoomPresentation(int w, int h) : width(w), height(h) {
        // Preserve Doom's native status art from x=0..250. The rightmost table
        // starts at x=251; never rescale the retained AMMO/HEALTH/ARMS/face/
        // ARMOR/keys strip to fill the panel.
        statusSourceWidth = 251;
        statusWidth = statusSourceWidth * width / 280;
        statusX = (width - statusWidth) / 2;
        statusHeight = 32 * width / 280;
        ammoHeight = 10 * width / 280;
        uiHeight = width * 3 / 4;
        uiY = (height - uiHeight) / 2;
        // Build the horizontal coordinate maps once, not in every pixel of every
        // frame. The status map also exchanges the original ARMS and face strips.
        for (int x = 0; x < width; ++x) {
            uiSourceX[x] = x * 320 / width;
            worldSourceX[x] = 20 + x * 280 / width;
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
    }

    template<class Pixel, class Background>
    void row(const Pixel* source, bool uiMode, int physicalY, Pixel* row, Background background) const {
        const int outputWidth = width, outputHeight = height;
        const int y = physicalY;
        const int worldHeight = outputHeight - ammoHeight - statusHeight;
        if (uiMode) {
            if (y < uiY || y >= uiY + uiHeight) {
                for (int x = 0; x < outputWidth; ++x) row[x] = 0;
            } else {
                const int sourceY = (y - uiY) * 200 / uiHeight;
                for (int x = 0; x < outputWidth; ++x) row[x] = source[sourceY * 320 + uiSourceX[x]];
            }
        } else if (y < worldHeight) {
            const int sourceY = y * 208 / worldHeight;
            for (int x = 0; x < outputWidth; ++x) row[x] = source[sourceY * 320 + worldSourceX[x]];
        } else if (y < worldHeight + ammoHeight) {
            const int y = physicalY - worldHeight;
            for (int x = 0; x < outputWidth; x++) {
                const int ammoType = x * 4 / outputWidth;
                const int withinCell = x - ammoType * outputWidth / 4;
                const int cellWidth = outputWidth / 4;
                const int sourceX = 250 + withinCell * 70 / cellWidth;
                if (y < 2 || y >= ammoHeight - 2) {
                    const int stoneY = y < 2 ? y : 32 - (ammoHeight - y);
                    row[x] = background(sourceX, stoneY);
                } else if (withinCell < 2 || (ammoType == 3 && withinCell >= cellWidth - 2)) {
                    // The original key/table divider is a two-pixel
                    // light-and-dark edge, repeated for each cell.
                    row[x] = background(249 + withinCell % 2,
                                                  y * 31 / (ammoHeight - 1));
                } else {
                    const int sourceY = 213 + ammoType * 6
                                      + (y - 2) * 6 / (ammoHeight - 4);
                    row[x] = source[sourceY * 320 + sourceX];
                }
            }
        } else {
            const int y = physicalY - worldHeight - ammoHeight;
            const int sourceY = 208 + y * 32 / statusHeight;
            for (int x = 0; x < outputWidth; x++) {
                if (x < statusX) {
                    // Match the clean right end cap; x=292..305 contains
                    // the carved slash ornament Anton does not want here.
                    row[x] = background(305 + x * 14 / statusX, sourceY - 208);
                    continue;
                } else if (x >= statusX + statusWidth) {
                    const int rightWidth = outputWidth - statusX - statusWidth;
                    row[x] = background(305 + (x - statusX - statusWidth) * 15 / rightWidth,
                                                  sourceY - 208);
                    continue;
                }
                const int sourceX = statusSourceX[x];
                row[x] = source[sourceY * 320 + sourceX];
            }
        }
    }
};

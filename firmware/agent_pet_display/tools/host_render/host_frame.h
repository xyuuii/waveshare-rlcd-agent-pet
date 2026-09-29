#pragma once

#include <string>

// Writes the current panel buffer (400x300 landscape, as seen on the RLCD)
// to a binary PBM file. Returns false when the file cannot be written.
bool hostWritePanelPbm(const std::string& path);

// Counts dark pixels inside a rectangle; handy for layout assertions.
int hostCountDarkPixels(int x, int y, int w, int h);

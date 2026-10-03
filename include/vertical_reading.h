#pragma once
#include "app.h"

void drawReadingToggle(App& app);
bool readingToggleClick(App& app, int x, int y);
bool verticalReadingKey(App& app, WPARAM key);
void renderVerticalReading(App& app);
void turnVerticalPage(App& app, int direction);
void prepareVerticalReading(App& app);

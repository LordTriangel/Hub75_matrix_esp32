#ifndef DISPLAY_RENDERER_H
#define DISPLAY_RENDERER_H

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>
#include <Fonts/IBMPlexMono24pt7b.h>
#include "system_state.h"

// Constants for Layout
#define PANEL_WIDTH 64
#define PANEL_HEIGHT 64
#define PANELS_CHAINED 3

struct Particle {
  float x;
  float y;
  float speed;
  uint16_t color;
  bool active;
};

class DisplayRenderer {
public:
    DisplayRenderer(MatrixPanel_I2S_DMA* display);

    void drawSun(int strahlStatus);
    void drawBattery(const SystemState& state);
    void drawBattery2(const SystemState& state, bool forceRedraw);
    void drawWeather(const SystemState& state, int x, int y);
    void drawClock();
    void drawMatrix3(const SystemState& state); // [NEW] Matrix 3 Pixel Rain
    
    // Helpers
    void clearScreen();
    void clearRect(int x, int y, int w, int h);
    void fadeOut(int targetMin = 0);
    void fadeIn(int targetMax);
    
private:
    MatrixPanel_I2S_DMA* dma_display;
    
    // Internal trackers for redraw optimization
    float lastDrawnSoc;
    int lastWChgCnt;
    String lastTimeStr;
    Particle particles[60]; // [NEW] Particles for rain effect
    
    int _currentBrightness; // Track current brightness locally

    uint16_t color565(uint8_t r, uint8_t g, uint8_t b);
};

#endif

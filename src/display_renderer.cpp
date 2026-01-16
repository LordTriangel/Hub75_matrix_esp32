#include "display_renderer.h"

DisplayRenderer::DisplayRenderer(MatrixPanel_I2S_DMA* display) : dma_display(display) {
    lastDrawnSoc = -999.0;
    lastWChgCnt = -1;
    lastTimeStr = "";
    _currentBrightness = 160; 
    
    // Initialize Particles
    for(int i=0; i<60; i++) {
        particles[i].active = false;
        particles[i].x = 0;
        particles[i].y = 0;
        particles[i].speed = 0;
        particles[i].color = 0;
    }
}

uint16_t DisplayRenderer::color565(uint8_t r, uint8_t g, uint8_t b) {
    return dma_display->color565(r, g, b);
}

void DisplayRenderer::clearScreen() {
    dma_display->fillScreen(0);
}

void DisplayRenderer::clearRect(int x, int y, int w, int h) {
    dma_display->fillRect(x, y, w, h, 0);
}

void DisplayRenderer::drawSun(int strahlStatus) {
    int centerX = 20; 
    int centerY = 20; 
    int sonnenRadius = 7; 

    uint16_t sonnenFarbe = color565(255, 255, 0);

    // 1. Die gelbe Kugel
    dma_display->fillCircle(centerX, centerY, sonnenRadius, sonnenFarbe);

    // 2. Die Strahlen
    if (strahlStatus > 0) {
        int strahlLaenge = strahlStatus * 4;      
        int strahlStartOffset = sonnenRadius + 2; 

        for (int i = 0; i < 6; i++) {
            float winkel = (i * 60.0) * (M_PI / 180.0);
            int x_start = centerX + cos(winkel) * strahlStartOffset;
            int y_start = centerY + sin(winkel) * strahlStartOffset;
            int x_end = centerX + cos(winkel) * (strahlStartOffset + strahlLaenge);
            int y_end = centerY + sin(winkel) * (strahlStartOffset + strahlLaenge);

            dma_display->drawLine(x_start, y_start, x_end, y_end, sonnenFarbe);
        }
    }
}

void DisplayRenderer::drawBattery(const SystemState& state) {
    // 1. Definiere Farben
    uint16_t weiss = color565(255, 255, 255);
    uint16_t rot = color565(255, 0, 0);
    uint16_t gelb = color565(255, 255, 0);
    uint16_t gruen = color565(0, 255, 0);
    uint16_t schwarz = 0; 
    
    // 2. Position
    int batt_breite = 10; 
    int batt_hoehe = 18;  
    int batt_x = 51;      
    int batt_y = 4;       

    float soc = state.batt_soc;

    // 3. Bestimme Füllfarbe
    uint16_t fuellFarbe;
    if (soc <= 20.0) fuellFarbe = rot;
    else if (soc <= 60.0) fuellFarbe = gelb;
    else fuellFarbe = gruen;

    if (soc < 0.0) fuellFarbe = schwarz; 

    // 4. Zeichne das weiße Gehäuse
    dma_display->fillRect(batt_x + 3, batt_y - 2, 4, 2, weiss);
    dma_display->drawRect(batt_x, batt_y, batt_breite, batt_hoehe, weiss);

    // 5. Zeichne die Füllung
    int innen_x = batt_x + 1;
    int innen_y = batt_y + 1;
    int innen_breite = batt_breite - 2;
    int maxInnenHoehe = batt_hoehe - 2; 

    int aktuelleFuellHoehe = (soc / 100.0) * maxInnenHoehe;
    if (aktuelleFuellHoehe > maxInnenHoehe) aktuelleFuellHoehe = maxInnenHoehe;
    if (aktuelleFuellHoehe < 0) aktuelleFuellHoehe = 0;

    int y_start_fuellung = innen_y + (maxInnenHoehe - aktuelleFuellHoehe);

    dma_display->fillRect(innen_x, innen_y, innen_breite, maxInnenHoehe - aktuelleFuellHoehe, schwarz);
    dma_display->fillRect(innen_x, y_start_fuellung, innen_breite, aktuelleFuellHoehe, fuellFarbe);
}

void DisplayRenderer::drawBattery2(const SystemState& state, bool forceRedraw) {
    // Check if redraw needed
    bool socChanged = abs(state.batt_soc - lastDrawnSoc) >= 0.1;
    bool chargeStatusChanged = (state.w_chg_cnt != lastWChgCnt);

    if (!forceRedraw && !socChanged && !chargeStatusChanged) {
        return;
    }

    dma_display->setFont(NULL); 
    int gesamtBreite = PANEL_WIDTH * PANELS_CHAINED; 
    
    int topBarHeight = 4;
    int gap = 1; 
    int socStartY = topBarHeight + gap; 
    int socHeight = 64 - socStartY;     
    
    uint16_t weiss = color565(255, 255, 255);
    uint16_t schwarz = color565(0, 0, 0);
    uint16_t gruen = color565(0, 255, 0); 
    uint16_t orange = color565(255, 140, 0); 

    float soc = state.batt_soc;
    if (soc < 0) soc = 0; 
    if (soc > 100) soc = 100;
    
    lastDrawnSoc = soc; 
    lastWChgCnt = state.w_chg_cnt;

    if (forceRedraw) {
        dma_display->fillScreen(0);
    }

    // A) TOP BARS
    int barGap = 2;
    int barWidth = (gesamtBreite - barGap) / 2; 
    
    uint16_t c1 = (state.w_chg_cnt >= 1) ? orange : gruen;
    uint16_t c2 = (state.w_chg_cnt >= 2) ? orange : gruen;

    dma_display->fillRoundRect(0, 0, barWidth, topBarHeight, 2, c1);
    dma_display->fillRoundRect(gesamtBreite - barWidth, 0, barWidth, topBarHeight, 2, c2);

    // B) SOC ANZEIGE
    dma_display->drawRect(0, socStartY, gesamtBreite, socHeight, weiss);
    
    int segmentWidth = 30;
    int gapWidth = 2;
    int startX = 1; 
    
    int totalActivePixels = 180;
    int pixelsToFill = (int)((soc / 100.0) * totalActivePixels);
    
    int x = startX;
    int drawY = socStartY + 1;     
    int drawH = socHeight - 2;     
    
    for (int i = 0; i < 6; i++) {
        int pixelsInThisSegment = 0;
        
        if (pixelsToFill >= segmentWidth) {
            pixelsInThisSegment = segmentWidth;
            pixelsToFill -= segmentWidth;
        } else if (pixelsToFill > 0) {
            pixelsInThisSegment = pixelsToFill;
            pixelsToFill = 0;
        }
        
        if (pixelsInThisSegment > 0) {
            dma_display->fillRect(x, drawY, pixelsInThisSegment, drawH, weiss);
        }
        
        if (pixelsInThisSegment < segmentWidth) {
            dma_display->fillRect(x + pixelsInThisSegment, drawY, segmentWidth - pixelsInThisSegment, drawH, schwarz);
        }
        
        x += segmentWidth;
        
        if (i < 5) {
            dma_display->fillRect(x, drawY, gapWidth, drawH, schwarz);
            x += gapWidth;
        }
    }

    // 3. Textfarbe basierend auf Akku-Stand
    uint16_t textFarbe;
    if (soc > 60.0) textFarbe = color565(0, 150, 0); 
    else if (soc > 20.0) textFarbe = color565(200, 180, 0); 
    else textFarbe = color565(255, 0, 0); 

    dma_display->setFont(&IBMPlexMono24pt7b);
    dma_display->setTextSize(1);
    dma_display->setTextColor(textFarbe);

    String prozentText = String((int)soc) + "%";
    
    // Text Bounds berechnen für echtes Zentrieren
    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(prozentText, 0, 0, &x1, &y1, &w, &h);

    int textX = (gesamtBreite - w) / 2;
    
    // Vertikal zentrieren mit korrekter Formel (Bounding Box Alignment)
    // Box Start: socStartY
    // Box Höhe: socHeight
    // Formel: y = BoxStart + (BoxHöhe - TextHöhe) / 2 - y1
    int textY = socStartY + (socHeight - h) / 2 - y1;

    dma_display->setCursor(textX, textY);
    dma_display->print(prozentText.c_str());
}

void DisplayRenderer::drawWeather(const SystemState& state, int x, int y) {
    // Falls noch keine gültigen Daten (-99.0) vorliegen, nichts zeichnen
    if (state.temperature == -99.0) return; 

    dma_display->setFont(NULL); // Standard-Schriftart sicherstellen
    
    uint16_t farbe = color565(255, 255, 255);
    // Farbcodierung für Temperatur (Blau < 10°C, Orange > 25°C)
    if (state.temperature < 10) farbe = color565(0, 100, 255);
    if (state.temperature > 25) farbe = color565(255, 100, 0);

    dma_display->setTextSize(1);
    dma_display->setTextColor(farbe);
    dma_display->setCursor(x, y);
    dma_display->print((int)round(state.temperature));
    dma_display->write(247); // Grad-Symbol
    dma_display->print("C");
}

void DisplayRenderer::drawClock() {
    dma_display->setFont(NULL); // Standard-Schriftart sicherstellen

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        // Falls NTP noch nicht gesynced hat
        dma_display->setCursor(5, 40);
        dma_display->setTextSize(1);
        dma_display->setTextColor(color565(255, 0, 0)); 
        dma_display->print("Zeit...");
        return;
    }

    char timeString[6]; 
    strftime(timeString, sizeof(timeString), "%H:%M", &timeinfo);

    // Blinkender Doppelpunkt (alle gerade Sekunden ausblenden)
    // Erzeugt den klassischen "Ticken"-Effekt
    if (timeinfo.tm_sec % 2 == 0) {
        timeString[2] = ' '; // Doppelpunkt durch Leerzeichen ersetzen
    }

    dma_display->setTextSize(2);                                     
    dma_display->setTextColor(color565(255, 255, 255)); 
  
    // Positionierung
    int x_pos = 4;
    int y_pos = 45; 

    dma_display->setCursor(x_pos, y_pos);
    dma_display->print(timeString);
}

// --- Fade Effekte ---

// Blendet das Display weich aus (dunkler werden)
// Blendet das Display weich aus (dunkler werden)
// Blockiert kurzzeitig, da delay() genutzt wird (gewollt für sauberen Effekt)
void DisplayRenderer::fadeOut(int targetMin) {
    int startBrightness = _currentBrightness;
    
    // Wenn wir schon dunkel genug sind, brich ab
    if (startBrightness <= targetMin) return;

    // Schrittweite berechnen (ca 10 Schritte für einen schnellen Fade)
    int steps = 15;
    int stepSize = (startBrightness - targetMin) / steps;
    if (stepSize < 1) stepSize = 1;

    for (int b = startBrightness; b >= targetMin; b -= stepSize) {
        dma_display->setBrightness8(b); // setBrightness8 erlaubt 0-255
        _currentBrightness = b;
        delay(5); // 5ms * 15 steps = ~75ms FadeOut
    }
    // Sicherstellen, dass Ziel erreicht ist
    dma_display->setBrightness8(targetMin);
    _currentBrightness = targetMin;
}

// Blendet das Display weich ein (heller werden)
// Blendet das Display weich ein (heller werden)
void DisplayRenderer::fadeIn(int targetMax) {
    int currentBrightness = _currentBrightness;
    
    // Wenn wir schon hell genug sind
    if (currentBrightness >= targetMax) {
        dma_display->setBrightness8(targetMax); 
        _currentBrightness = targetMax;
        return;
    }

    // Sync hardware to start value to prevent jumps
    dma_display->setBrightness8(currentBrightness);

    int steps = 15;
    int stepSize = (targetMax - currentBrightness) / steps;
    if (stepSize < 1) stepSize = 1;

    for (int b = currentBrightness; b <= targetMax; b += stepSize) {
        dma_display->setBrightness8(b);
        _currentBrightness = b;
        delay(5); // ~75ms FadeIn
    }
    dma_display->setBrightness8(targetMax);
    _currentBrightness = targetMax;
}

void DisplayRenderer::drawMatrix3(const SystemState& state) {
    // 3rd Panel: x = 128 to 191
    int offset_x = 128; // Start of 3rd panel
    
    // --- 1. Draw Text ---
    
    // PV Power (Green) - Top Half
    // Top Half Height = 32. Center Y = 16.
    // Text Size 2 (Default Font) -> Approx 14px high.
    // 16 - 7 = 9 (Top Y).
    
    dma_display->setTextSize(2);
    uint16_t colorPV = dma_display->color565(0, 255, 0);
    
    String pvStr = String(1 * abs((int)state.pv_power)) + "W";
    // if (state.pv_power < 0) pvStr = "0W"; // Removed to allow negative display 
    
    int16_t x1, y1;
    uint16_t w, h;
    dma_display->getTextBounds(pvStr, offset_x, 0, &x1, &y1, &w, &h);
    
    // Center X
    int xPosPV = offset_x + (64 - w)/2;
    // Center Y in Top Half (0-31) -> Target Center 16. 
    // GFX SetCursor is Top-Left. So 16 - h/2.
    int yPosPV = 16 - h/2;

    dma_display->setTextColor(colorPV);
    dma_display->setCursor(xPosPV, yPosPV);
    dma_display->print(pvStr);

    // Consumption (Orange) - Bottom Half
    // Bottom Half Height = 32 (32-63). Center Y = 48.
    
    uint16_t colorUse = dma_display->color565(255, 165, 0); // Orange
    // Always Negative
    String useStr = String(-1 * abs((int)state.verbrauchsleistung)) + "W";
    // if (state.verbrauchsleistung < 0) useStr = "0W"; // Removed
    
    dma_display->getTextBounds(useStr, offset_x, 0, &x1, &y1, &w, &h);
    int xPosUse = offset_x + (64 - w)/2;
    int yPosUse = 48 - h/2;
    
    dma_display->setTextColor(colorUse);
    dma_display->setCursor(xPosUse, yPosUse); 
    dma_display->print(useStr);


    // --- 2. Particle System Update & Draw ---
    
    // A. Spawn Particles (Faster!)
    // Spawn up to 2 frames?
    for (int k=0; k<2; k++) {
        if (random(100) < 70) { // 70% chance
            for (int i = 0; i < 60; i++) {
                if (!particles[i].active) {
                    particles[i].active = true;
                    if (random(2) == 0) {
                        // Type: PV (Green) - Top
                        particles[i].color = dma_display->color565(0, 200 + random(55), 0); 
                        particles[i].x = offset_x + random(64);
                        particles[i].y = 0;
                        particles[i].speed = 0.8 + (random(10) / 10.0); // Slower: 0.8 - 1.8
                    } else {
                        // Type: Usage (Orange) - Bottom
                        particles[i].color = dma_display->color565(255, 140 + random(40), 0); 
                        particles[i].x = offset_x + random(64); 
                        particles[i].y = 55; 
                        particles[i].speed = 0.8 + (random(10) / 10.0);
                    }
                    break;
                }
            }
        }
    }

    // B. Update & Draw Particles with Trails
    for (int i = 0; i < 60; i++) {
        if (particles[i].active) {
            int px = (int)particles[i].x;
            int py = (int)particles[i].y;
            
            // Draw Head
            dma_display->drawPixel(px, py, particles[i].color);
            
            // Draw Trail (Fading)
            // Pixel 1 behind
            int py1 = py - (int)particles[i].speed; 
            if (py1 >= 0 && py1 < 64) {
                 // Dim color? Just use predefined less bright?
                 // Simple hack: Shift right to dim component
                 // Or just draw slightly darker if possible. 
                 // Let's just draw same color but every other pixel? No.
                 // Let's hardcode a dim color logic or just skip trails for now if complex.
                 // Trail IS vital for "movement" perception.
                 // Use 50% brightness
                 uint16_t c = particles[i].color;
                 uint16_t dimC = (c >> 1) & 0x7BEF; // Quick 50% dim (lossy but fast)
                 dma_display->drawPixel(px, py1, dimC);
                 
                 // Pixel 2 behind
                 int py2 = py1 - (int)particles[i].speed;
                 if (py2 >= 0 && py2 < 64) {
                     uint16_t dimC2 = (dimC >> 1) & 0x7BEF; // 25%
                     dma_display->drawPixel(px, py2, dimC2);
                 }
            }

            particles[i].y += particles[i].speed;
            
            bool isGreen = ((particles[i].color >> 5) & 0x3F) > 30 && (particles[i].color & 0xF800) == 0; 
            
            if (isGreen) {
                if (particles[i].y > 8) particles[i].active = false; 
            } else {
                if (particles[i].y >= 64) particles[i].active = false;
            }
        }
    }
}

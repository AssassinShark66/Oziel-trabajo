#ifndef CAMCOLOR_H
#define CAMCOLOR_H

#include "movimiento.h"
#include "pcam.h"
#include "Scaning.h"

extern uint8_t cameraAddress[];      
extern bool inCalibrateColor;        
extern bool needsRedraw;             
extern Adafruit_SSD1306 display;     
extern AccelStepper stepper;         
extern void returnToHomePosition();  

struct MenuItem; 
extern MenuItem* currentMenu;
extern MenuItem menuCalibrar[];
extern int currentMenuSize;

enum ColorCalibState { CC_MENU, CC_READY, CC_MOVING, CC_WAITING, CC_DONE, CC_ERROR };
ColorCalibState ccState = CC_MENU;
int ccSelection = 1;
bool ccResponseReceived = false;

void drawCamColorMenu() {
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0); display.print("CALIBRAR COLORES"); display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    if (ccSelection == 1) { display.fillRect(0, 20, 128, 14, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); } 
    else display.setTextColor(SSD1306_WHITE);
    display.setCursor(5, 23); display.print("Ficha 1 (Jugador)");
    if (ccSelection == 2) { display.fillRect(0, 38, 128, 14, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); } 
    else display.setTextColor(SSD1306_WHITE);
    display.setCursor(5, 41); display.print("Ficha 2 (Maquina)");
    display.setTextColor(SSD1306_WHITE); display.drawFastHLine(0, 54, 128, SSD1306_WHITE);
    display.setCursor(18, 57); display.print("BACK"); display.setCursor(85, 57); display.print("ENTER");
    display.display();
}

void drawCalibrationProcess() {
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0); display.print("CALIBRANDO FICHA "); display.print(ccSelection); display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    if (ccState == CC_READY) {
        display.setCursor(10, 25); display.print("Coloca ficha centro"); display.setCursor(10, 35); display.print("y pulsa INICIAR");
        display.fillRect(20, 48, 88, 14, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK);
        display.setCursor(45, 51); display.print("INICIAR");
    } else if (ccState == CC_MOVING) { display.setCursor(20, 30); display.print("Moviendo Robot..."); }
    else if (ccState == CC_WAITING) {
        display.setCursor(25, 20); display.print("Analizando...");
        int barWidth = (millis() / 50) % 100; 
        display.drawRect(14, 40, 100, 10, SSD1306_WHITE); display.fillRect(16, 42, barWidth, 6, SSD1306_WHITE);
    } else if (ccState == CC_DONE) {
        display.setTextSize(2); display.setCursor(5, 25); display.print("COMPLETADO");
        display.setTextSize(1); display.setCursor(30, 50); display.print("Pulsa BACK");
    }
    display.display();
}

void drawCamColor() { 
    if (ccState == CC_MENU) drawCamColorMenu(); 
    else { drawCalibrationProcess(); if (ccState == CC_WAITING) needsRedraw = true; } 
}

void handleCamColorEncoder(int delta) {
    if (ccState == CC_MENU && delta != 0) { if (delta > 0) ccSelection = 2; else ccSelection = 1; needsRedraw = true; }
}

void handleCamColorSelect() {
    if (ccState == CC_MENU) { ccState = CC_READY; needsRedraw = true; }
    else if (ccState == CC_READY) {
        ccState = CC_MOVING; drawCamColor();
        moverEje(2, getPCamValue(2)); delay(300); 
        moverEje(0, getPCamValue(0)); moverEje(1, getPCamValue(1)); 
        while (stepper.distanceToGo() != 0) { stepper.run(); } delay(500);
        ccState = CC_WAITING; drawCamColor();
        char cmd[10]; sprintf(cmd, "CAL_%d", ccSelection);
        esp_now_send(cameraAddress, (uint8_t *)cmd, strlen(cmd));
    }
}

void handleCamColorBack() {
    if (ccState == CC_MENU) { inCalibrateColor = false; currentMenu = menuCalibrar; currentMenuSize = 6; returnToHomePosition(); needsRedraw = true; } 
    else { ccState = CC_MENU; returnToHomePosition(); needsRedraw = true; }
}

void handleCamColorResponse(const char* msg) { if (ccState == CC_WAITING) { ccState = CC_DONE; needsRedraw = true; } }
#endif
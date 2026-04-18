#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AiEsp32RotaryEncoder.h>
#include <AccelStepper.h> 
#include <EEPROM.h>

// Definición de estructura de Menú
typedef void (*ActionFunction)();
struct MenuItem {
    const char* name;
    MenuItem* children;
    int numChildren;
    ActionFunction action;
    MenuItem* parent;
};

// INCLUDES DEL PROYECTO
// El orden es importante para las dependencias
#include "tablero.h"
#include "inicial.h"
#include "pcam.h"
#include "riel.h"
#include "movimiento.h" 
#include "juego.h"      
#include "Scaning.h"    
#include "camcolor.h"   

// --- CONFIGURACIÓN PANTALLA ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- CONFIGURACIÓN ENCODER Y BOTONES ---
#define ENCODER_CLK_PIN 16
#define ENCODER_DT_PIN 17
#define BTN_SELECT_PIN 18
#define BTN_BACK_PIN 19
#define BUZZER_PIN 4  

#define ENCODER_STEPS 4
AiEsp32RotaryEncoder encoder(ENCODER_CLK_PIN, ENCODER_DT_PIN, -1, -1, ENCODER_STEPS);
long lastEncoderValue = 0;

// --- VARIABLES DE ESTADO UI ---
bool needsRedraw = true;
unsigned long lastSelectTime = 0;
const long BUTTON_COOLDOWN = 200;
int menuScrollOffset = 0;

unsigned long backBtnTimer = 0;
bool backBtnPressed = false;
bool longPressHandled = false;
const int LONG_PRESS_TIME = 3000;

unsigned long buzzerTimer = 0;
bool buzzerActive = false;
const int BEEP_DURATION = 15;

// --- VARIABLES DE ESTADO DE CALIBRACIÓN Y JUEGO ---
bool inGameMode = false;         
bool inCalibrateColor = false;   
bool inCalibrateTablero = false;
int currentCalibPos = 0;
int calibXYZ = 0;

bool inCalibrateInicial = false;
int calibInicialAxis = 0;

bool inCalibratePCam = false;
int calibPCamAxis = 0;

bool inCalibrateRiel = false;
int rielPos = 0;    
int rielAxis = 0;

bool inCalibrateStepper = false;
int stepperMenuSelection = 0; 
int stepperCalibState = 0; // 0=Menu, 1=Calibrando(Bloqueante), 2=Resultado
bool isAdjustingSpeed = false;
long newMaxStepsFound = 0;

// --- PROTOTIPOS ---
void actionEnterGame();
void actionCalibrarPosIni();
void actionCalibrarRiel();
void actionCalibrarPosCam();
void actionMenuCamColor(); 
void actionCalibrarTablero();
void actionMotorPasos();
void returnToHomePosition();
void handleBackShort();
void handleBackLong(); 
void drawMenu(); 

// --- FUNCIONES AUXILIARES ---
void beep() {
    digitalWrite(BUZZER_PIN, LOW); delayMicroseconds(100);
    digitalWrite(BUZZER_PIN, HIGH);
    buzzerTimer = millis(); buzzerActive = true;            
}
void beepLong() { 
    digitalWrite(BUZZER_PIN, LOW); delayMicroseconds(100); 
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200); digitalWrite(BUZZER_PIN, LOW);
}

// --- DEFINICIÓN DE MENÚS ---
MenuItem menuCalibrar[] = {
    {"Posicion Inicial", NULL, 0, actionCalibrarPosIni, NULL},
    {"Motor a pasos", NULL, 0, actionMotorPasos, NULL},
    {"Tablero", NULL, 0, actionCalibrarTablero, NULL},
    {"Riel Fichas", NULL, 0, actionCalibrarRiel, NULL},
    {"Posicion Camara", NULL, 0, actionCalibrarPosCam, NULL},
    {"Colores Camara", NULL, 0, actionMenuCamColor, NULL} 
};

MenuItem menuPrincipal[] = {
    {"Jugar", NULL, 0, actionEnterGame, NULL}, 
    {"Calibrar", menuCalibrar, 6, NULL, NULL}
};

MenuItem* currentMenu = menuPrincipal;
int currentMenuSize = 2;
int currentSelection = 0;

// --- ACCIONES DEL MENÚ ---
void actionEnterGame() { 
    inGameMode = true; initTicTacToe(); needsRedraw = true; 
    encoder.setEncoderValue(0); lastEncoderValue = 0;
}
void actionMenuCamColor() { 
    inCalibrateColor = true; ccState = CC_MENU; ccSelection = 1; needsRedraw = true; 
    encoder.setEncoderValue(0); lastEncoderValue = 0; 
}
void actionCalibrarPosIni() { 
    inCalibrateInicial = true; calibInicialAxis = 0; 
    encoder.setEncoderValue(0); lastEncoderValue = 0; needsRedraw = true; 
    moverEje(0, getInitValue(0)); moverEje(1, getInitValue(1)); moverEje(2, getInitValue(2)); 
}
void actionCalibrarRiel() { 
    inCalibrateRiel = true; rielPos = 0; rielAxis = 0; 
    encoder.setEncoderValue(0); lastEncoderValue = 0; needsRedraw = true; 
    moverEje(0, getRielValue(0, 0)); moverEje(1, getRielValue(0, 1)); moverEje(2, getRielValue(0, 2)); 
}
void actionCalibrarPosCam() { 
    inCalibratePCam = true; calibPCamAxis = 0; 
    encoder.setEncoderValue(0); lastEncoderValue = 0; needsRedraw = true; 
    moverEje(0, getPCamValue(0)); moverEje(1, getPCamValue(1)); moverEje(2, getPCamValue(2)); 
}
void actionCalibrarTablero() { 
    inCalibrateTablero = true; currentCalibPos = 0; calibXYZ = 0; 
    encoder.setEncoderValue(0); lastEncoderValue = 0; needsRedraw = true; 
    moverEje(0, getCalibValue(0, 0)); moverEje(1, getCalibValue(0, 1)); moverEje(2, getCalibValue(0, 2)); 
}
void actionMotorPasos() { 
    loadStepperConfig(); 
    inCalibrateStepper = true; stepperMenuSelection = 0; stepperCalibState = 0;
    newMaxStepsFound = max_pasos_x; 
    needsRedraw = true; 
}

void returnToHomePosition() { 
    moverEje(0, getInitValue(0)); moverEje(1, getInitValue(1)); moverEje(2, getInitValue(2));
}

void setParent(MenuItem* menu, int numItems, MenuItem* parentMenu) {
    for (int i = 0; i < numItems; i++) {
        menu[i].parent = parentMenu;
        if (menu[i].children != NULL) setParent(menu[i].children, menu[i].numChildren, menu);
    }
}

void IRAM_ATTR readEncoderISR() { encoder.readEncoder_ISR(); }

// --- DIBUJADO DE MENÚS Y PANTALLAS ---
// (Estas funciones se mantienen casi idénticas al original, solo asegurando llamadas correctas)

void drawCalibrateStepper() {
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0); display.print("CALIBRAR MOTOR"); display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    
    if (stepperMenuSelection == 0 && !isAdjustingSpeed) { 
        display.fillRect(0, 16, 128, 12, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); 
    } else display.setTextColor(SSD1306_WHITE);
    display.setCursor(2, 17); display.print("Iniciar Calibracion");
    
    if (stepperMenuSelection == 1 && !isAdjustingSpeed) { 
        display.fillRect(0, 30, 128, 22, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK);
    } else if (isAdjustingSpeed) { 
        display.setTextColor(SSD1306_WHITE); display.drawRect(0, 30, 128, 22, SSD1306_WHITE); 
    } else display.setTextColor(SSD1306_WHITE);
    
    display.setCursor(2, 31); display.print("Velocidad: "); int current_speed = getMotorSpeedPercent(); display.print(current_speed); display.print("%");
    int bar_width = map(current_speed, 0, 100, 0, 70);
    uint16_t bar_color = (stepperMenuSelection == 1 && !isAdjustingSpeed) ? SSD1306_BLACK : SSD1306_WHITE;
    display.drawRect(2, 41, 72, 10, bar_color); display.fillRect(3, 42, bar_width, 8, bar_color);
    
    display.setTextColor(SSD1306_WHITE); display.setCursor(0, 54);
    if (stepperCalibState == 0) { display.print("Pasos: "); display.print(max_pasos_x); }
    else if (stepperCalibState == 1) { display.print("Calibrando..."); }
    else if (stepperCalibState == 2) { display.print("Nuevos: "); display.print(newMaxStepsFound); }
    display.display(); needsRedraw = false;
}

void drawCalibrateTablero() {
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0); display.print("TABLERO"); display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    int cs = 12, ox = 5, oy = 18;
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
        int x = ox + c * cs, y = oy + r * cs;
        if ((r * 3 + c) == currentCalibPos) display.fillRect(x, y, cs, cs, SSD1306_WHITE); else display.drawRect(x, y, cs, cs, SSD1306_WHITE);
    }
    int valX=getCalibValue(currentCalibPos,0), valY=getCalibValue(currentCalibPos,1), valZ=getCalibValue(currentCalibPos,2);
    display.setTextColor(SSD1306_WHITE); 
    display.setCursor(60, 20); if(calibXYZ==0) { display.fillRect(60,20,60,10,SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); }
    display.print("X: "); display.print(valX); display.setTextColor(SSD1306_WHITE); 
    display.setCursor(60, 33); if(calibXYZ==1) { display.fillRect(60,33,60,10,SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); }
    display.print("Y: "); display.print(valY); display.setTextColor(SSD1306_WHITE); 
    display.setCursor(60, 46); if(calibXYZ==2) { display.fillRect(60,46,60,10,SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); }
    display.print("Z: "); display.print(valZ); display.setTextColor(SSD1306_WHITE); display.display(); needsRedraw = false;
}

void drawCalibrateInicial() {
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0); display.print("POSICION INICIAL"); display.drawFastHLine(0,10,128,SSD1306_WHITE);
    display.drawLine(20,30,30,20,SSD1306_WHITE); display.drawLine(30,20,40,30,SSD1306_WHITE); display.drawRect(22,30,16,14,SSD1306_WHITE);
    int valX=getInitValue(0), valY=getInitValue(1), valZ=getInitValue(2);
    display.setCursor(60,20); if(calibInicialAxis==0) {display.fillRect(60,20,60,10,SSD1306_WHITE); display.setTextColor(SSD1306_BLACK);} display.print("X: "); display.print(valX); display.setTextColor(SSD1306_WHITE);
    display.setCursor(60,33); if(calibInicialAxis==1) {display.fillRect(60,33,60,10,SSD1306_WHITE); display.setTextColor(SSD1306_BLACK);} display.print("Y: "); display.print(valY); display.setTextColor(SSD1306_WHITE);
    display.setCursor(60,46); if(calibInicialAxis==2) {display.fillRect(60,46,60,10,SSD1306_WHITE); display.setTextColor(SSD1306_BLACK);} display.print("Z: "); display.print(valZ); display.setTextColor(SSD1306_WHITE);
    display.display(); needsRedraw=false;
}

void drawCalibratePCam() {
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0); display.print("POSICION CAMARA"); display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    display.drawCircle(30,35,8,SSD1306_WHITE); display.fillCircle(30,35,3,SSD1306_WHITE);
    int valX = getPCamValue(0), valY = getPCamValue(1), valZ = getPCamValue(2);
    display.setCursor(60, 20); if (calibPCamAxis == 0) { display.fillRect(60, 20, 60, 10, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); } display.print("X: "); display.print(valX); display.setTextColor(SSD1306_WHITE);
    display.setCursor(60, 33); if (calibPCamAxis == 1) { display.fillRect(60, 33, 60, 10, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); } display.print("Y: "); display.print(valY); display.setTextColor(SSD1306_WHITE);
    display.setCursor(60, 46); if (calibPCamAxis == 2) { display.fillRect(60, 46, 60, 10, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); } display.print("Z: "); display.print(valZ); display.setTextColor(SSD1306_WHITE);
    display.display(); needsRedraw = false;
}

void drawCalibrateRiel() {
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0); display.print("CALIBRACION RIEL"); display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    int cs = 12, ox = 5, oy = 18;
    for (int r = 0; r < 3; r++) for (int c = 0; c < 2; c++) {
        int idx = r * 2 + c;
        int x = ox + c * cs, y = oy + r * cs; if (idx >= 5) continue;
        if (idx == rielPos) display.fillRect(x, y, cs, cs, SSD1306_WHITE); else display.drawRect(x, y, cs, cs, SSD1306_WHITE);
    }
    int valX = getRielValue(rielPos, 0), valY = getRielValue(rielPos, 1), valZ = getRielValue(rielPos, 2);
    display.setCursor(60, 20); if (rielAxis == 0) { display.fillRect(60, 20, 60, 10, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); } display.print("X: "); display.print(valX); display.setTextColor(SSD1306_WHITE);
    display.setCursor(60, 33); if (rielAxis == 1) { display.fillRect(60, 33, 60, 10, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); } display.print("Y: "); display.print(valY); display.setTextColor(SSD1306_WHITE);
    display.setCursor(60, 46); if (rielAxis == 2) { display.fillRect(60, 46, 60, 10, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); } display.print("Z: "); display.print(valZ); display.setTextColor(SSD1306_WHITE);
    display.display(); needsRedraw = false;
}

void drawMenu() {
    if (inGameMode) { drawTicTacToeScreen(); return; }
    if (inCalibrateColor) { drawCamColor(); return; }
    if (inCalibrateStepper) { drawCalibrateStepper(); return; } 
    if (inCalibrateTablero) { drawCalibrateTablero(); return; }
    if (inCalibrateInicial) { drawCalibrateInicial(); return; }
    if (inCalibratePCam) { drawCalibratePCam(); return; }
    if (inCalibrateRiel) { drawCalibrateRiel(); return; }

    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    if (currentMenu == menuPrincipal) display.print("MENU PRINCIPAL");
    else if (currentMenu == menuCalibrar) display.print("CALIBRACION");
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    int maxItems = 3;
    if (currentSelection < menuScrollOffset) menuScrollOffset = currentSelection;
    if (currentSelection >= menuScrollOffset + maxItems) menuScrollOffset = currentSelection - maxItems + 1;
    for (int i = 0; i < maxItems; i++) {
        int idx = menuScrollOffset + i;
        if (idx >= currentMenuSize) break;
        int yPos = 17 + i * 14;
        if (idx == currentSelection) { display.fillRect(0, yPos, 120, 12, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); } 
        else display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, yPos); display.print(currentMenu[idx].name); display.setTextColor(SSD1306_WHITE);
    }
    display.drawRect(120, 18, 6, 34, SSD1306_WHITE);
    int indicatorHeight = max(6, 34 / currentMenuSize);
    int indicatorY = 18 + (34 * currentSelection) / currentMenuSize;
    if (indicatorY > 18 + 34 - indicatorHeight) indicatorY = 18 + 34 - indicatorHeight;
    display.fillRect(121, indicatorY, 4, indicatorHeight, SSD1306_WHITE);
    display.drawFastHLine(0, 54, 128, SSD1306_WHITE);
    display.setCursor(18, 57); display.print("BACK"); display.setCursor(85, 57); display.print("ENTER");
    display.display(); needsRedraw = false;
}

// --- GESTIÓN DE INPUTS ---
void handleEncoder() {
    if (inGameMode) { 
        long prev = lastEncoderValue;
        handleTicTacToeEncoder(); lastEncoderValue = encoder.readEncoder(); 
        if (prev != lastEncoderValue && game_currentState != STATE_ROBOT_MOVING) beep(); 
        return;
    }
    if (stepperCalibState == 1) return; // Bloqueado si calibrando motor

    long newValue = encoder.readEncoder();
    long delta = newValue - lastEncoderValue;
    if (delta > 1) delta = 1; if (delta < -1) delta = -1;
    if (delta != 0) {
        beep(); 
        if (inCalibrateColor) { handleCamColorEncoder(delta); }
        else if (inCalibrateStepper) {
            if (isAdjustingSpeed) {
                int spd = getMotorSpeedPercent() + delta;
                setMotorSpeedPercent(spd); stepperCalibState = 0;
            } else {
                if (stepperMenuSelection == 0 && delta > 0) stepperMenuSelection = 1;
                else if (stepperMenuSelection == 1 && delta < 0) stepperMenuSelection = 0;
                stepperCalibState = 0;
            }
        }
        else if (inCalibrateTablero) {
            int val = getCalibValue(currentCalibPos, calibXYZ) + delta;
            setCalibValue(currentCalibPos, calibXYZ, val); moverEje(calibXYZ, val);
        }
        else if (inCalibrateInicial) {
            int val = getInitValue(calibInicialAxis) + delta;
            setInitValue(calibInicialAxis, val); moverEje(calibInicialAxis, val);
        }
        else if (inCalibratePCam) {
            int val = getPCamValue(calibPCamAxis) + delta;
            setPCamValue(calibPCamAxis, val); moverEje(calibPCamAxis, val);
        }
        else if (inCalibrateRiel) {
             int val = getRielValue(rielPos, rielAxis) + delta;
             setRielValue(rielPos, rielAxis, val); moverEje(rielAxis, val);
        }
        else {
            if (delta > 0 && currentSelection < currentMenuSize - 1) currentSelection++;
            else if (delta < 0 && currentSelection > 0) currentSelection--;
        }
        needsRedraw = true;
    }
    lastEncoderValue = newValue;
}

void handleSelect() {
    if (!inGameMode || game_currentState != STATE_ROBOT_MOVING) beep();

    if (inGameMode) { handleTicTacToeSelect(); return; }
    if (stepperCalibState == 1) return;

    if (inCalibrateColor) { handleCamColorSelect(); return; }

    if (inCalibrateStepper) {
        if (isAdjustingSpeed) { isAdjustingSpeed = false; saveStepperConfig(); needsRedraw = true; } 
        else if (stepperMenuSelection == 0) { 
            digitalWrite(BUZZER_PIN, LOW); buzzerActive = false; 
            stepperCalibState = 1; needsRedraw = true; 
            drawMenu(); // Dibujar "Calibrando..." antes de bloquear
            newMaxStepsFound = runFullCalibration(); // Proceso BLOQUEANTE
            stepperCalibState = 2; needsRedraw = true;
        } else if (stepperMenuSelection == 1) { isAdjustingSpeed = true; needsRedraw = true; }
        return;
    }

    if (inCalibrateTablero) {
        calibXYZ++;
        if (calibXYZ > 2) {
            calibXYZ = 0; currentCalibPos++;
            if (currentCalibPos >= 9) {
                inCalibrateTablero = false; saveAllCalibrations(); returnToHomePosition();
                currentCalibPos = 0; currentSelection = 2; currentMenu = menuCalibrar; currentMenuSize = 6;
            } else {
                moverEje(0, getCalibValue(currentCalibPos, 0));
                moverEje(1, getCalibValue(currentCalibPos, 1));
                moverEje(2, getCalibValue(currentCalibPos, 2));
            }
        }
        needsRedraw = true; return;
    }
    
    if (inCalibrateInicial) {
         calibInicialAxis++;
         if (calibInicialAxis > 2) {
             calibInicialAxis = 0; inCalibrateInicial = false; saveInitialPos(); returnToHomePosition();
             currentSelection = 0; currentMenu = menuCalibrar; currentMenuSize = 6;
         }
         needsRedraw = true; return;
    }

    if (inCalibratePCam) {
        calibPCamAxis++;
        if (calibPCamAxis > 2) {
            calibPCamAxis = 0; inCalibratePCam = false; savePCamPos(); returnToHomePosition();
            currentSelection = 4; currentMenu = menuCalibrar; currentMenuSize = 6;
        }
        needsRedraw = true; return;
    }

    if (inCalibrateRiel) {
        rielAxis++;
        if (rielAxis > 2) {
            rielAxis = 0; rielPos++;
            if (rielPos >= 5) {
                inCalibrateRiel = false; saveRielPos(); returnToHomePosition();
                currentSelection = 3; currentMenu = menuCalibrar; currentMenuSize = 6;
            } else {
                 moverEje(0, getRielValue(rielPos, 0));
                 moverEje(1, getRielValue(rielPos, 1));
                 moverEje(2, getRielValue(rielPos, 2));
            }
        }
        needsRedraw = true; return;
    }

    MenuItem* item = &currentMenu[currentSelection];
    if (item->children != NULL) {
        currentMenu = item->children; currentMenuSize = item->numChildren;
        currentSelection = 0; menuScrollOffset = 0; 
        encoder.setEncoderValue(0); lastEncoderValue = 0;
    } else if (item->action != NULL) { item->action(); }
    needsRedraw = true;
}

void handleBackShort() {
    beep();
    if (inGameMode) { if (handleTicTacToeBack()) { inGameMode = false; needsRedraw = true; encoder.setEncoderValue(0); lastEncoderValue = 0; } return; }
    if (stepperCalibState == 1) return;
    if (inCalibrateColor) { handleCamColorBack(); return; }
    if (inCalibrateStepper) {
        if (isAdjustingSpeed) { isAdjustingSpeed = false; loadStepperConfig(); needsRedraw = true; return; }
        inCalibrateStepper = false; saveStepperConfig(); returnToHomePosition();
        needsRedraw = true; return;
    }

    if (inCalibrateTablero) {
        if (calibXYZ > 0) calibXYZ--;
        else { inCalibrateTablero = false; saveAllCalibrations(); returnToHomePosition(); }
        needsRedraw = true; return;
    }
    if (inCalibrateInicial) {
        if (calibInicialAxis > 0) calibInicialAxis--;
        else { inCalibrateInicial = false; saveInitialPos(); returnToHomePosition(); }
        needsRedraw = true; return;
    }
    if (inCalibratePCam) {
        if (calibPCamAxis > 0) calibPCamAxis--;
        else { inCalibratePCam = false; savePCamPos(); returnToHomePosition(); }
        needsRedraw = true; return;
    }
    if (inCalibrateRiel) {
        if (rielAxis > 0) rielAxis--;
        else { inCalibrateRiel = false; saveRielPos(); returnToHomePosition(); }
        needsRedraw = true; return;
    }

    if (currentMenu[0].parent != NULL) {
        currentMenu = currentMenu[0].parent;
        if (currentMenu == menuPrincipal) currentMenuSize = 2;
        if (currentMenu == menuCalibrar) currentMenuSize = 6;
        currentSelection = 0; encoder.setEncoderValue(0);
        lastEncoderValue = 0; needsRedraw = true;
    }
}

void handleBackLong() {
    beepLong(); 
    inCalibrateTablero = false; inCalibrateInicial = false;
    inCalibratePCam = false; inCalibrateRiel = false; 
    inCalibrateStepper = false; inCalibrateColor = false; inGameMode = false;
    saveAllCalibrations(); saveInitialPos(); savePCamPos(); saveRielPos(); saveStepperConfig();
    currentMenu = menuPrincipal; currentMenuSize = 2; currentSelection = 0;
    returnToHomePosition();
    needsRedraw = true;
}

// --- SETUP Y LOOP PRINCIPAL ---

void setup() {
    Serial.begin(115200);
    EEPROM.begin(512);
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { while(1); }
    pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
    
    initMovimiento(); 
    initScaning(); 
    
    encoder.begin(); encoder.setup(readEncoderISR); encoder.setAcceleration(0);
    pinMode(BTN_SELECT_PIN, INPUT_PULLUP); pinMode(BTN_BACK_PIN, INPUT_PULLUP);
    
    setParent(menuPrincipal, 2, NULL);
    initCalibrations(); initInitialPos(); initPCamPos(); initRielPos();
    
    returnToHomePosition(); 
    drawMenu();
}

void loop() {
    // --- NUEVO: GESTIÓN DE MOVIMIENTO NO BLOQUEANTE ---
    // 1. Siempre dar oportunidad al motor de moverse si tiene un destino pendiente
    bool motorMoving = stepper.run();

    // 2. Actualizar la máquina de estados del robot (Juego)
    // Esto hace avanzar la secuencia de juego (coger ficha, dejarla, etc.) sin bloquear.
    updateRobotSequence();

    // --- GESTIÓN DE ENTRADAS ---
    // Solo procesamos entradas si NO estamos en la calibración especial de motor (que es bloqueante por seguridad)
    if (stepperCalibState != 1) {
        
        handleEncoder();

        if (digitalRead(BTN_SELECT_PIN) == LOW) { 
            if (millis() - lastSelectTime > BUTTON_COOLDOWN) { handleSelect(); lastSelectTime = millis(); } 
        }
        
        if (digitalRead(BTN_BACK_PIN) == LOW) {
            if (!backBtnPressed) { backBtnPressed = true; backBtnTimer = millis(); longPressHandled = false; } 
            else { if (!longPressHandled && (millis() - backBtnTimer > LONG_PRESS_TIME)) { handleBackLong(); longPressHandled = true; } }
        } else {
            if (backBtnPressed) {
                backBtnPressed = false;
                if (!longPressHandled && (millis() - backBtnTimer > 50)) handleBackShort(); 
            }
        }
    }

    // --- GESTIÓN DE ENERGÍA ---
    // Si el motor ya llegó a su destino, no estamos en calibración bloqueante, 
    // y la secuencia del robot ha terminado (IDLE), apagamos los drivers para ahorrar energía/calor.
    if (!motorMoving && stepperCalibState != 1 && robotState == R_IDLE) {
         stepper.disableOutputs();
    }

    // --- GESTIÓN DE UI ---
    if (inCalibrateStepper && stepperCalibState != 1) needsRedraw = true;
    
    if (buzzerActive) { 
        if (millis() - buzzerTimer > BEEP_DURATION) { digitalWrite(BUZZER_PIN, LOW); buzzerActive = false; } 
    }
    
    if (needsRedraw) drawMenu();
}
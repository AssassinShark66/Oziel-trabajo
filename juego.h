#ifndef JUEGO_H
#define JUEGO_H

#include <AiEsp32RotaryEncoder.h>
#include "movimiento.h"
#include "riel.h"
#include "tablero.h"

// --- DECLARACIONES EXTERNAS ---
extern Adafruit_SSD1306 display;
extern bool needsRedraw;
extern bool realizarEscaneo(); 
extern void returnToHomePosition();
extern void beep(); 
extern AiEsp32RotaryEncoder encoder; 
extern long lastEncoderValue;        

// --- VARIABLES DEL JUEGO ---
int machineTokenIndex = 0; 
char game_board[3][3];     
char game_winner = ' ';
int game_playerSelectCursor = 0; 

enum GameState {
    STATE_TITLE, STATE_PLAYER_SELECT, STATE_HUMAN_THINKING, STATE_ROBOT_MOVING, STATE_GAME_OVER
};
GameState game_currentState = STATE_TITLE;

struct Move { int x; int y; };

// Prototipos
void drawTicTacToeScreen(); 

// --- LÓGICA DEL JUEGO (Minimax/Reglas) ---

bool game_checkWin(char p) {
    for (int i=0; i<3; i++) { 
        if(game_board[i][0]==p && game_board[i][1]==p && game_board[i][2]==p) return true;
        if(game_board[0][i]==p && game_board[1][i]==p && game_board[2][i]==p) return true; 
    }
    if(game_board[0][0]==p && game_board[1][1]==p && game_board[2][2]==p) return true;
    if(game_board[0][2]==p && game_board[1][1]==p && game_board[2][0]==p) return true;
    return false;
}

bool game_checkTie() {
    if(game_winner!=' ') return false;
    for(int y=0; y<3; y++) for(int x=0; x<3; x++) if(game_board[y][x]==' ') return false;
    return true;
}

Move game_findBestMove() {
    // 1. Ganar
    for(int y=0;y<3;y++) for(int x=0;x<3;x++) if(game_board[y][x]==' ') {
        game_board[y][x]='O'; if(game_checkWin('O')) { game_board[y][x]=' '; Move m; m.x=x; m.y=y; return m; } game_board[y][x]=' ';
    }
    // 2. Bloquear
    for(int y=0;y<3;y++) for(int x=0;x<3;x++) if(game_board[y][x]==' ') {
        game_board[y][x]='X'; if(game_checkWin('X')) { game_board[y][x]=' '; Move m; m.x=x; m.y=y; return m; } game_board[y][x]=' ';
    }
    // 3. Estrategia (Centro y esquinas)
    if(game_board[1][1]==' ') { Move m; m.x=1; m.y=1; return m; }
    if(game_board[0][0]==' ') { Move m; m.x=0; m.y=0; return m; } 
    if(game_board[0][2]==' ') { Move m; m.x=2; m.y=0; return m; }
    if(game_board[2][0]==' ') { Move m; m.x=0; m.y=2; return m; } 
    if(game_board[2][2]==' ') { Move m; m.x=2; m.y=2; return m; }
    // 4. Lo que sobre
    for(int y=0;y<3;y++) for(int x=0;x<3;x++) if(game_board[y][x]==' ') { Move m; m.x=x; m.y=y; return m; }
    Move m; m.x=-1; m.y=-1; return m; 
}

// --- MÁQUINA DE ESTADOS DEL ROBOT (NUEVA LÓGICA NO BLOQUEANTE) ---

enum RobotState {
    R_IDLE,             // Esperando ordenes
    R_MOVE_TO_RIEL,     // Moviendo XY hacia el riel
    R_WAIT_RIEL,        // Esperando llegar al riel
    R_LOWER_Z_PICK,     // Bajando brazo
    R_MAGNET_ON,        // Activando imán
    R_RAISE_Z_PICK,     // Subiendo brazo con ficha
    R_MOVE_TO_BOARD,    // Moviendo XY al tablero
    R_WAIT_BOARD,       // Esperando llegar al tablero
    R_LOWER_Z_PLACE,    // Bajando brazo para dejar
    R_MAGNET_OFF,       // Soltando ficha
    R_RAISE_Z_PLACE,    // Subiendo brazo vacío
    R_RETURN_HOME       // Regresando a casa
};

RobotState robotState = R_IDLE;
unsigned long stateTimer = 0; // Para sustituir delay()
int targetGridX, targetGridY; // Variables para recordar a dónde vamos

// Función para INICIAR el movimiento (solo configura, no bloquea)
void startRobotMove(int gridX, int gridY) {
    if (machineTokenIndex >= 5) return;
    targetGridX = gridX;
    targetGridY = gridY;
    robotState = R_MOVE_TO_RIEL; // Iniciamos la secuencia
}

// Esta función se debe llamar CONSTANTEMENTE en el loop() principal
void updateRobotSequence() {
    // Si estamos esperando un tiempo (equivalente a delay no bloqueante)
    if (millis() < stateTimer) return; 

    switch (robotState) {
        case R_IDLE:
            // No hacer nada
            break;

        case R_MOVE_TO_RIEL:
            // Configurar movimiento hacia el riel
            moverEje(2, 0); // Z arriba por seguridad
            moverEje(0, getRielValue(machineTokenIndex, 0)); // X
            moverEje(1, getRielValue(machineTokenIndex, 1)); // Y
            robotState = R_WAIT_RIEL;
            break;

        case R_WAIT_RIEL:
            // Verificamos si el motor llegó a su destino usando la nueva función de movimiento.h
            if (!isMotorRunning()) {
                // Si ya llegó, pasamos al siguiente estado
                robotState = R_LOWER_Z_PICK;
                stateTimer = millis() + 200; // Pequeña pausa de estabilización
            }
            break;

        case R_LOWER_Z_PICK:
            moverEje(2, getRielValue(machineTokenIndex, 2)); // Bajar Z
            robotState = R_MAGNET_ON;
            stateTimer = millis() + 500; // Tiempo para bajar
            break;

        case R_MAGNET_ON:
            setMagnet(true);
            robotState = R_RAISE_Z_PICK;
            stateTimer = millis() + 500; // Tiempo para asegurar agarre
            break;

        case R_RAISE_Z_PICK:
            moverEje(2, 0); // Subir Z
            robotState = R_MOVE_TO_BOARD;
            stateTimer = millis() + 500; // Tiempo para subir
            break;

        case R_MOVE_TO_BOARD:
            // Calcular posición destino en el tablero
            { // Llaves necesarias para declarar variables dentro del case
                int posIndex = targetGridY * 3 + targetGridX;
                moverEje(0, getCalibValue(posIndex, 0));
                moverEje(1, getCalibValue(posIndex, 1));
            }
            robotState = R_WAIT_BOARD;
            break;

        case R_WAIT_BOARD:
            if (!isMotorRunning()) {
                robotState = R_LOWER_Z_PLACE;
                stateTimer = millis() + 200;
            }
            break;

        case R_LOWER_Z_PLACE:
            {
                int posIndex = targetGridY * 3 + targetGridX;
                moverEje(2, getCalibValue(posIndex, 2));
            }
            robotState = R_MAGNET_OFF;
            stateTimer = millis() + 500;
            break;

        case R_MAGNET_OFF:
            setMagnet(false);
            robotState = R_RAISE_Z_PLACE;
            stateTimer = millis() + 500;
            break;

        case R_RAISE_Z_PLACE:
            moverEje(2, 0);
            robotState = R_RETURN_HOME;
            stateTimer = millis() + 200;
            break;

        case R_RETURN_HOME:
            returnToHomePosition();
            machineTokenIndex++; // Siguiente ficha
            
            // Lógica de fin de turno del juego (AHORA SE HACE AL FINAL DEL MOVIMIENTO)
            game_board[targetGridY][targetGridX] = 'O';
            
            // Verificar victorias aquí o cambiar estado de juego
            if (game_checkWin('O')) { 
                game_winner = 'O'; game_currentState = STATE_GAME_OVER; 
            } else if (game_checkTie()) { 
                game_winner = 'T'; game_currentState = STATE_GAME_OVER; 
            } else { 
                game_currentState = STATE_HUMAN_THINKING; 
                realizarEscaneo(); // Ojo: realizarEscaneo sigue siendo bloqueante (por ahora está bien)
            }
            
            needsRedraw = true;
            robotState = R_IDLE; // Secuencia terminada
            break;
    }
}

// --- CONTROL DEL TURNO ---

void runMachineTurn() {
    game_currentState = STATE_ROBOT_MOVING;
    needsRedraw = true;

    // Escaneo previo para asegurar que el humano no hizo trampa o movió algo
    if (!realizarEscaneo()) { } 

    // Verificamos si el humano ganó con su último movimiento antes de que el robot intente mover
    if (game_checkWin('X')) { game_winner = 'X'; game_currentState = STATE_GAME_OVER; beep(); return; }
    if (game_checkTie()) { game_winner = 'T'; game_currentState = STATE_GAME_OVER; beep(); return; }

    Move bestMove = game_findBestMove();

    if (bestMove.x != -1) {
        // AQUÍ ESTÁ EL CAMBIO PRINCIPAL:
        // En lugar de llamar a executePhysicalMove() que bloquea todo,
        // iniciamos la máquina de estados.
        startRobotMove(bestMove.x, bestMove.y);
        
        // NOTA: Ya no actualizamos game_board ni game_currentState aquí inmediatamente.
        // Eso sucederá automáticamente cuando updateRobotSequence llegue a R_RETURN_HOME.
    } else {
        game_winner = 'T'; game_currentState = STATE_GAME_OVER;
    }
    
    beep(); 
    needsRedraw = true;
}

// --- INTERFAZ GRÁFICA (OLED) ---

void drawMiniBoard(int offX, int offY) {
    int cs = 18; 
    display.drawFastVLine(offX+cs, offY, cs*3, SSD1306_WHITE); 
    display.drawFastVLine(offX+cs*2, offY, cs*3, SSD1306_WHITE);
    display.drawFastHLine(offX, offY+cs, cs*3, SSD1306_WHITE); 
    display.drawFastHLine(offX, offY+cs*2, cs*3, SSD1306_WHITE);
    for(int y=0;y<3;y++) for(int x=0;x<3;x++) {
        if(game_board[y][x] != ' ') { 
            display.setCursor(offX + x*cs + 5, offY + y*cs + 4); 
            display.setTextSize(2); display.print(game_board[y][x]); 
        }
    }
}

void drawGameTitle() { 
    display.clearDisplay(); display.setTextSize(2); display.setTextColor(SSD1306_WHITE);
    display.setCursor(20,20); display.print("TIC TAC"); 
    display.setCursor(40,40); display.print("TOE"); 
    display.setTextSize(1); display.setCursor(25,55); display.print("PRESS ENTER");
    display.display(); 
}

void drawPlayerSelect() {
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 0); display.print("QUIEN EMPIEZA?"); display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    if (game_playerSelectCursor == 0) { display.fillRect(0, 20, 128, 14, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); } else display.setTextColor(SSD1306_WHITE);
    display.setCursor(5, 23); display.print("TU (Humano)");
    if (game_playerSelectCursor == 1) { display.fillRect(0, 38, 128, 14, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK); } else display.setTextColor(SSD1306_WHITE);
    display.setCursor(5, 41); display.print("MAQUINA (Robot)");
    display.display();
}

void drawHumanTurn() {
    display.clearDisplay(); drawMiniBoard(0, 5);
    display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
    display.setCursor(65, 5); display.print("TU TURNO");
    display.setCursor(65, 25); display.print("Mueve y"); display.setCursor(65, 35); display.print("presiona");
    display.fillRect(62, 48, 60, 12, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK);
    display.setCursor(70, 50); display.print("ENTER"); display.display();
}

void drawRobotMoving() {
    display.clearDisplay(); drawMiniBoard(0, 5);
    display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
    display.setCursor(65, 15); display.print("ROBOT"); display.setCursor(65, 30); display.print("PENSANDO");
    // Animación simple mientras se mueve (ahora sí se verá fluida gracias a la FSM)
    int dots = (millis() / 500) % 4; for(int i=0; i<dots; i++) display.print(".");
    display.display();
}

void drawGameOverScreen() { 
    display.clearDisplay(); display.setTextColor(SSD1306_WHITE); drawMiniBoard(0, 5);
    display.setTextSize(2); display.setCursor(60,10); 
    if (game_winner == 'X') display.print("WIN!"); else if (game_winner == 'O') display.print("LOSE"); else display.print("DRAW");
    display.setTextSize(1); display.setCursor(60, 40); display.print("Back=Exit"); display.display(); 
}

void drawScanning() {
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(30, 30); display.print("INICIANDO..."); display.setCursor(20, 45); display.print("Escaneando mesa");
    display.display();
}

void initTicTacToe() {
    for(int y=0;y<3;y++) for(int x=0;x<3;x++) game_board[y][x]=' ';
    machineTokenIndex = 0; game_winner = ' '; game_currentState = STATE_TITLE; needsRedraw = true;
    robotState = R_IDLE; // Asegurar que el robot empieza quieto
}

void drawTicTacToeScreen() { 
    if(!needsRedraw && game_currentState != STATE_ROBOT_MOVING) return; 
    switch(game_currentState) {
        case STATE_TITLE: drawGameTitle(); break;
        case STATE_PLAYER_SELECT: drawPlayerSelect(); break;
        case STATE_HUMAN_THINKING: drawHumanTurn(); break;
        case STATE_ROBOT_MOVING: drawRobotMoving(); needsRedraw=true; break; 
        case STATE_GAME_OVER: drawGameOverScreen(); break;
    } 
    // Si el robot se está moviendo, siempre necesitamos redibujar para la animación de puntos "..."
    if (game_currentState != STATE_ROBOT_MOVING) needsRedraw=false;
}

void handleTicTacToeEncoder() {
    if (game_currentState == STATE_PLAYER_SELECT) {
        long newValue = encoder.readEncoder(); long delta = newValue - lastEncoderValue;
        if (delta != 0) { if (delta > 0) game_playerSelectCursor = 1; else game_playerSelectCursor = 0; needsRedraw = true; }
    }
}

void handleTicTacToeSelect() {
    switch(game_currentState) {
        case STATE_TITLE: 
            game_currentState = STATE_PLAYER_SELECT; game_playerSelectCursor = 0; 
            break;
        case STATE_PLAYER_SELECT:
            for(int y=0;y<3;y++) for(int x=0;x<3;x++) game_board[y][x]=' ';
            machineTokenIndex = 0; game_winner = ' '; 
            drawScanning(); realizarEscaneo(); 
            if (game_playerSelectCursor == 0) { game_currentState = STATE_HUMAN_THINKING; needsRedraw = true; } 
            else { runMachineTurn(); }
            break;
        case STATE_HUMAN_THINKING:
            runMachineTurn();
            break;
        case STATE_GAME_OVER:
            game_currentState = STATE_TITLE;
            break;
    } 
    needsRedraw = true;
}

bool handleTicTacToeBack() { 
    if(game_currentState == STATE_PLAYER_SELECT) { game_currentState = STATE_TITLE; needsRedraw = true; return false; }
    return true; 
}

#endif
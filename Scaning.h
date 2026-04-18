#ifndef SCANING_H
#define SCANING_H

#include <esp_now.h>
#include <WiFi.h>
#include "pcam.h"
#include "movimiento.h"
#include "juego.h"
#include "camcolor.h"

uint8_t cameraAddress[] = {0xA0, 0xA3, 0xB3, 0x30, 0x5A, 0xF0}; 

typedef struct struct_message { char board[9]; } struct_message;
struct_message incomingBoard;
bool dataReceived = false;

void handleCamColorResponse(const char* msg);

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    char msg[len + 1]; memcpy(msg, incomingData, len); msg[len] = '\0';
    if (strcmp(msg, "CAL_OK") == 0) { handleCamColorResponse(msg); return; }
    if (len == sizeof(incomingBoard)) { memcpy(&incomingBoard, incomingData, sizeof(incomingBoard)); dataReceived = true; }
}

void initScaning() {
    WiFi.mode(WIFI_STA); 
    if (esp_now_init() != ESP_OK) return;
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_peer_info_t peerInfo; memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, cameraAddress, 6); peerInfo.channel = 0; peerInfo.encrypt = false; peerInfo.ifidx = WIFI_IF_STA; 
    esp_now_add_peer(&peerInfo);
}

bool realizarEscaneo() {
    int camZ = getPCamValue(2); int camX = getPCamValue(0); int camY = getPCamValue(1);
    moverEje(2, camZ); delay(300);
    moverEje(0, camX); moverEje(1, camY);
    while (stepper.distanceToGo() != 0) { stepper.run(); } delay(500);

    const char *msg = "SCAN"; dataReceived = false;
    esp_err_t result = esp_now_send(cameraAddress, (uint8_t *)msg, strlen(msg));
    if (result != ESP_OK) return false;

    unsigned long startWait = millis();
    while (!dataReceived) { if (millis() - startWait > 6000) return false; delay(10); }

    Serial.println("--- FUSIONANDO DATOS ---");
    for (int y = 0; y < 3; y++) {
        for (int x = 0; x < 3; x++) {
            int idx = y * 3 + x;
            char camData = incomingBoard.board[idx];
            if (game_board[y][x] == 'O') { Serial.print("[O*] "); } 
            else { game_board[y][x] = camData; Serial.print("["); Serial.print(game_board[y][x]); Serial.print("] "); }
        }
        Serial.println();
    }
    needsRedraw = true; return true;
}
#endif
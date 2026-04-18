#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <EEPROM.h>
#include "img_converters.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <math.h> 

// --- 1. CONFIGURACIÓN DE RED ---
const char* ssid = "Robot_Hotspot";      
const char* password = "12345678";       
uint8_t robotAddress[] = {0xE8, 0x6B, 0xEA, 0xF6, 0xA0, 0xD4}; 

// --- 2. PINES ---
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#define FLASH_GPIO_NUM 4

// --- 3. AJUSTES DE VISIÓN ---
#define GRID_INSET 10       
#define COLOR_TOLERANCE 70  // Tolerancia de coincidencia (más alto = más permisivo)

// --- 4. ESTRUCTURAS ---
typedef struct struct_message { char board[9]; } struct_message;
struct_message myBoardData;

// Estructura para guardar 5 muestras de color por ficha
struct ColorProfile { 
    uint8_t samples[5][3]; // 5 muestras, cada una con R, G, B
};

ColorProfile profileFicha1; 
ColorProfile profileFicha2; 

struct ColorRGB { int r; int g; int b; };

WebServer server(80);
bool scanRequested = false;
int calibrateRequest = 0; 
bool manualFlashState = false; 

// --- PROTOTIPOS ---
void configurarCamara();
void handleCapture();
void handleRoot();
void handleFlash(); 
void processImageBlob(uint8_t* rgb, int w, int h);
void initColors();
void saveProfile(int ficha, ColorProfile profile);
ColorRGB getRegionColor(uint8_t* img, int w, int h, int cx, int cy, int size);
void drawLine(uint8_t* img, int w, int h, int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b);
void drawCross(uint8_t* img, int w, int h, int x, int y, int size, uint8_t r, uint8_t g, uint8_t b);
void fillRect(uint8_t* img, int w, int h, int cx, int cy, int size, uint8_t r, uint8_t g, uint8_t b);

// --- IMPLEMENTACIÓN GRÁFICA ---
void drawLine(uint8_t* img, int w, int h, int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b) {
  int dx=abs(x1-x0), sx=x0<x1?1:-1; int dy=-abs(y1-y0), sy=y0<y1?1:-1; int err=dx+dy, e2;
  while(1){ if(x0>=0&&x0<w&&y0>=0&&y0<h){ int idx=(y0*w+x0)*3; img[idx]=r; img[idx+1]=g; img[idx+2]=b; }
  if(x0==x1&&y0==y1)break; e2=2*err; if(e2>=dy){ err+=dy; x0+=sx; } if(e2<=dx){ err+=dx; y0+=sy; } }
}
void drawCross(uint8_t* img, int w, int h, int x, int y, int size, uint8_t r, uint8_t g, uint8_t b) {
    drawLine(img, w, h, x-size, y, x+size, y, r, g, b);
    drawLine(img, w, h, x, y-size, x, y+size, r, g, b);
}
void fillRect(uint8_t* img, int w, int h, int cx, int cy, int size, uint8_t r, uint8_t g, uint8_t b) {
   int hs = size/2; 
   for(int y=cy-hs; y<cy+hs; y++) for(int x=cx-hs; x<cx+hs; x++) 
   if(x>=0 && x<w && y>=0 && y<h) { int idx=(y*w+x)*3; img[idx]=r; img[idx+1]=g; img[idx+2]=b; }
}
ColorRGB getRegionColor(uint8_t* img, int w, int h, int cx, int cy, int size) {
  long sumR=0, sumG=0, sumB=0; int count=0; int hs = size/2;
  for(int y=cy-hs; y<cy+hs; y+=2) {
      for(int x=cx-hs; x<cx+hs; x+=2) {
          if(x>=0&&x<w&&y>=0&&y<h) {
              int idx=(y*w+x)*3; 
              uint8_t r = img[idx]; uint8_t g = img[idx+1]; uint8_t b = img[idx+2];
              // Filtro Anti-Brillo: Ignorar blancos puros
              if (r < 240 || g < 240 || b < 240) { sumR+=r; sumG+=g; sumB+=b; count++; }
          }
      }
  }
  if(count==0) return {0,0,0}; return { (int)(sumR/count), (int)(sumG/count), (int)(sumB/count) };
}

// --- DETECTOR DE MARCO VERDE ---
bool isGreenFrame(uint8_t r, uint8_t g, uint8_t b) {
    if (r > 200 && g > 200 && b > 200) return false; // Ignorar blancos
    if (r < 30 && g < 30 && b < 30) return false;    // Ignorar negros profundos
    
    // Lógica de verde: G debe ser dominante o un gris verdoso oscuro
    if (g > r + 5 && g > b + 5) return true; 
    if (g >= r && g >= b && r < 100) return true; // Verde oscuro
    return false;
}

// --- COMPARADOR MULTI-PUNTO ---
// Compara el color detectado contra las 5 muestras guardadas.
// Si coincide con ALGUNA, retorna verdadero.
bool isProfileMatch(ColorRGB sample, ColorProfile profile) {
    for(int i=0; i<5; i++) {
        int targetR = profile.samples[i][0];
        int targetG = profile.samples[i][1];
        int targetB = profile.samples[i][2];
        
        int dist = sqrt(pow(sample.r - targetR, 2) + pow(sample.g - targetG, 2) + pow(sample.b - targetB, 2));
        
        if (dist < COLOR_TOLERANCE) return true; // ¡Coincide con una de las muestras!
    }
    return false;
}

// --- GESTIÓN DE MEMORIA EEPROM ---
void initColors() {
    EEPROM.begin(64); // Aumentamos tamaño para 30 bytes
    // Cargar Ficha 1 (Bytes 0-14)
    for(int i=0; i<5; i++) {
        profileFicha1.samples[i][0] = EEPROM.read(i*3 + 0);
        profileFicha1.samples[i][1] = EEPROM.read(i*3 + 1);
        profileFicha1.samples[i][2] = EEPROM.read(i*3 + 2);
    }
    // Cargar Ficha 2 (Bytes 15-29)
    int offset = 15;
    for(int i=0; i<5; i++) {
        profileFicha2.samples[i][0] = EEPROM.read(offset + i*3 + 0);
        profileFicha2.samples[i][1] = EEPROM.read(offset + i*3 + 1);
        profileFicha2.samples[i][2] = EEPROM.read(offset + i*3 + 2);
    }
}

void saveProfile(int ficha, ColorProfile profile) {
    int offset = (ficha == 1) ? 0 : 15;
    for(int i=0; i<5; i++) {
        EEPROM.write(offset + i*3 + 0, profile.samples[i][0]);
        EEPROM.write(offset + i*3 + 1, profile.samples[i][1]);
        EEPROM.write(offset + i*3 + 2, profile.samples[i][2]);
    }
    EEPROM.commit();
    
    // Actualizar RAM
    if (ficha == 1) profileFicha1 = profile;
    else profileFicha2 = profile;
}

// --- MOTOR DE VISIÓN (BLOB + PERFILES) ---
void processImageBlob(uint8_t* rgb, int w, int h) {
    int minX = w, maxX = 0, minY = h, maxY = 0, pointsFound = 0;
    int step = 4; 

    // 1. Detectar Borde Verde (Mancha)
    for (int y = 10; y < h - 10; y += step) {
        for (int x = 10; x < w - 10; x += step) {
            int idx = (y * w + x) * 3;
            if (isGreenFrame(rgb[idx], rgb[idx+1], rgb[idx+2])) {
                if (x < minX) minX = x; if (x > maxX) maxX = x;
                if (y < minY) minY = y; if (y > maxY) maxY = y;
                pointsFound++;
            }
        }
    }

    if (pointsFound < 100 || maxX <= minX || maxY <= minY) {
        minX = 60; maxX = 260; minY = 20; maxY = 220; // Fallback
    }

    // Dibujar Borde Detectado (Azul)
    drawLine(rgb, w, h, minX, minY, maxX, minY, 0, 0, 255);
    drawLine(rgb, w, h, maxX, minY, maxX, maxY, 0, 0, 255);
    drawLine(rgb, w, h, maxX, maxY, minX, maxY, 0, 0, 255);
    drawLine(rgb, w, h, minX, maxY, minX, minY, 0, 0, 255);

    // Calcular Rejilla
    int gridMinX = minX + GRID_INSET; int gridMaxX = maxX - GRID_INSET;
    int gridMinY = minY + GRID_INSET; int gridMaxY = maxY - GRID_INSET;
    int stepX = (gridMaxX - gridMinX) / 3; int stepY = (gridMaxY - gridMinY) / 3;

    // Dibujar Rejilla Verde
    drawLine(rgb, w, h, gridMinX + stepX, gridMinY, gridMinX + stepX, gridMaxY, 0, 255, 0);
    drawLine(rgb, w, h, gridMinX + stepX*2, gridMinY, gridMinX + stepX*2, gridMaxY, 0, 255, 0);
    drawLine(rgb, w, h, gridMinX, gridMinY + stepY, gridMaxX, gridMinY + stepY, 0, 255, 0);
    drawLine(rgb, w, h, gridMinX, gridMinY + stepY*2, gridMaxX, gridMinY + stepY*2, 0, 255, 0);

    // Analizar Fichas con Perfiles
    int startCX = gridMinX + (stepX / 2);
    int startCY = gridMinY + (stepY / 2);
    int cellIndex = 0;

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            int cx = startCX + (col * stepX);
            int cy = startCY + (row * stepY);
            char detected = ' ';

            if (cx > 0 && cx < w && cy > 0 && cy < h) {
                drawCross(rgb, w, h, cx, cy, 3, 150, 150, 150); 
                ColorRGB avg = getRegionColor(rgb, w, h, cx, cy, 14);
                
                // USAR COMPARADOR DE PERFILES
                if (isProfileMatch(avg, profileFicha1)) {
                    // Usar el color de la muestra central para dibujar
                    int r = profileFicha1.samples[0][0]; 
                    int g = profileFicha1.samples[0][1];
                    int b = profileFicha1.samples[0][2];
                    fillRect(rgb, w, h, cx, cy, 12, r, g, b);
                    detected = 'X';
                } else if (isProfileMatch(avg, profileFicha2)) {
                    int r = profileFicha2.samples[0][0]; 
                    int g = profileFicha2.samples[0][1];
                    int b = profileFicha2.samples[0][2];
                    fillRect(rgb, w, h, cx, cy, 12, r, g, b);
                    detected = 'O';
                }
            }
            myBoardData.board[cellIndex++] = detected;
        }
    }
}

// --- NUEVA LÓGICA DE CALIBRACIÓN MULTIPUNTO ---
void performCalibration(int ficha) {
    digitalWrite(FLASH_GPIO_NUM, HIGH); delay(5000); 
    camera_fb_t * dummy = esp_camera_fb_get(); if(dummy) esp_camera_fb_return(dummy);
    
    camera_fb_t * fb = esp_camera_fb_get(); 
    if (!fb) { digitalWrite(FLASH_GPIO_NUM, LOW); return; }
    
    size_t out_len = fb->width * fb->height * 3; 
    uint8_t * rgb_buf = (uint8_t*)ps_malloc(out_len);
    if (!rgb_buf) { esp_camera_fb_return(fb); digitalWrite(FLASH_GPIO_NUM, LOW); return; }

    fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb_buf); 
    esp_camera_fb_return(fb);
    
    // --- MUESTREO INTELIGENTE DE 5 PUNTOS ---
    // Asumimos que el usuario ha puesto fichas en el centro y las 4 esquinas del tablero
    // O al menos que ha puesto una ficha grande que cubre gran parte.
    
    // 1. Detectar dónde está el tablero primero para saber dónde muestrear
    // (Usamos una lógica simplificada de detección de bordes verdes aquí mismo)
    int minX = 320, maxX = 0, minY = 240, maxY = 0;
    for (int y = 20; y < 220; y += 10) {
        for (int x = 20; x < 300; x += 10) {
            int idx = (y * 320 + x) * 3;
            if (isGreenFrame(rgb_buf[idx], rgb_buf[idx+1], rgb_buf[idx+2])) {
                if(x < minX) minX=x; if(x > maxX) maxX=x;
                if(y < minY) minY=y; if(y > maxY) maxY=y;
            }
        }
    }
    // Si falla, usar centro por defecto
    if(maxX <= minX) { minX=60; maxX=260; minY=20; maxY=220; }

    // Definir los 5 puntos de muestreo (Insetados para caer en las casillas)
    int safeMinX = minX + 20; int safeMaxX = maxX - 20;
    int safeMinY = minY + 20; int safeMaxY = maxY - 20;
    int midX = (minX + maxX) / 2; int midY = (minY + maxY) / 2;

    ColorProfile newProfile;
    // Muestra 0: Centro
    ColorRGB c0 = getRegionColor(rgb_buf, 320, 240, midX, midY, 10);
    // Muestra 1: Arriba Izq
    ColorRGB c1 = getRegionColor(rgb_buf, 320, 240, safeMinX, safeMinY, 10);
    // Muestra 2: Arriba Der
    ColorRGB c2 = getRegionColor(rgb_buf, 320, 240, safeMaxX, safeMinY, 10);
    // Muestra 3: Abajo Izq
    ColorRGB c3 = getRegionColor(rgb_buf, 320, 240, safeMinX, safeMaxY, 10);
    // Muestra 4: Abajo Der
    ColorRGB c4 = getRegionColor(rgb_buf, 320, 240, safeMaxX, safeMaxY, 10);

    newProfile.samples[0][0]=c0.r; newProfile.samples[0][1]=c0.g; newProfile.samples[0][2]=c0.b;
    newProfile.samples[1][0]=c1.r; newProfile.samples[1][1]=c1.g; newProfile.samples[1][2]=c1.b;
    newProfile.samples[2][0]=c2.r; newProfile.samples[2][1]=c2.g; newProfile.samples[2][2]=c2.b;
    newProfile.samples[3][0]=c3.r; newProfile.samples[3][1]=c3.g; newProfile.samples[3][2]=c3.b;
    newProfile.samples[4][0]=c4.r; newProfile.samples[4][1]=c4.g; newProfile.samples[4][2]=c4.b;

    saveProfile(ficha, newProfile);
    
    free(rgb_buf);
    digitalWrite(FLASH_GPIO_NUM, LOW);
    
    const char* reply = "CAL_OK"; 
    esp_now_send(robotAddress, (uint8_t *)reply, strlen(reply));
}

// --- COMUNICACIÓN ---
void executeScanAndReply() {
    digitalWrite(FLASH_GPIO_NUM, HIGH); delay(5000); 
    camera_fb_t * dummy = esp_camera_fb_get(); if(dummy) esp_camera_fb_return(dummy);
    camera_fb_t * fb = esp_camera_fb_get(); 
    if (!fb) { digitalWrite(FLASH_GPIO_NUM, LOW); return; }
    size_t out_len = fb->width * fb->height * 3; 
    uint8_t * rgb_buf = (uint8_t*)ps_malloc(out_len);
    if (!rgb_buf) { esp_camera_fb_return(fb); digitalWrite(FLASH_GPIO_NUM, LOW); return; }
    fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb_buf); esp_camera_fb_return(fb);
    
    processImageBlob(rgb_buf, 320, 240); 
    
    free(rgb_buf); digitalWrite(FLASH_GPIO_NUM, LOW);
    esp_now_send(robotAddress, (uint8_t *) &myBoardData, sizeof(myBoardData));
    scanRequested = false;
}

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
    char msg[len + 1]; memcpy(msg, incomingData, len); msg[len] = '\0';
    if (strcmp(msg, "SCAN") == 0) scanRequested = true;
    else if (strcmp(msg, "CAL_1") == 0) calibrateRequest = 1;
    else if (strcmp(msg, "CAL_2") == 0) calibrateRequest = 2;
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<style>body{background-color:#121212;color:white;text-align:center;font-family:sans-serif;}
img{border:2px solid #03dac6;max-width:100%;}
.btn{background-color:#bb86fc;border:none;color:black;padding:15px 32px;font-size:16px;margin:10px;cursor:pointer;border-radius:8px;}
</style></head><body><h1>Robot Vision PRO MAX</h1><img src="/capture" id="cam" onload="reload()" onerror="reload()"/><br><br>
<button class="btn" onclick="toggleFlash()">TOGGLE FLASH</button>
<script>function reload(){setTimeout(function(){document.getElementById("cam").src="/capture?t="+new Date().getTime();},100);}
function toggleFlash(){ fetch('/flash'); }</script></body></html>)rawliteral";
  server.send(200, "text/html", html);
}

void handleFlash() { manualFlashState = !manualFlashState; digitalWrite(FLASH_GPIO_NUM, manualFlashState ? HIGH : LOW); server.send(200, "text/plain", "OK"); }

void handleCapture() {
  camera_fb_t * fb = esp_camera_fb_get(); if (!fb) { server.send(500, "text/plain", "Err"); return; }
  size_t out_len = fb->width * fb->height * 3; uint8_t * rgb_buf = (uint8_t*)ps_malloc(out_len);
  if (!rgb_buf) { esp_camera_fb_return(fb); server.send(500, "text/plain", "RAM"); return; }
  fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, rgb_buf); esp_camera_fb_return(fb);
  
  processImageBlob(rgb_buf, 320, 240); 
  
  uint8_t * jpg_buf = NULL; size_t jpg_len = 0;
  fmt2jpg(rgb_buf, out_len, 320, 240, PIXFORMAT_RGB888, 40, &jpg_buf, &jpg_len); free(rgb_buf);
  server.sendHeader("Content-Disposition", "inline; filename=v.jpg");
  server.send_P(200, "image/jpeg", (const char *)jpg_buf, jpg_len); free(jpg_buf);
}

void setupCamera() {
  camera_config_t config; config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM; config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM; config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; config.pixel_format = PIXFORMAT_JPEG; 
  config.frame_size = FRAMESIZE_QVGA; config.jpeg_quality = 12; config.fb_count = 1;
  esp_camera_init(&config);
  sensor_t * s = esp_camera_sensor_get(); if (s) { s->set_brightness(s, 1); s->set_contrast(s, 2); s->set_saturation(s, 2); }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); Serial.begin(115200);
  pinMode(FLASH_GPIO_NUM, OUTPUT); digitalWrite(FLASH_GPIO_NUM, LOW);
  initColors();
  WiFi.mode(WIFI_AP_STA); WiFi.softAP(ssid, password);
  delay(100); Serial.println(WiFi.softAPIP());
  if (esp_now_init() == ESP_OK) {
      esp_now_register_recv_cb(OnDataRecv);
      esp_now_peer_info_t peerInfo; memset(&peerInfo, 0, sizeof(peerInfo)); 
      memcpy(peerInfo.peer_addr, robotAddress, 6); peerInfo.channel = 0; peerInfo.encrypt = false; peerInfo.ifidx = WIFI_IF_AP; 
      esp_now_add_peer(&peerInfo);
  }
  setupCamera();
  server.on("/", handleRoot); server.on("/flash", handleFlash); server.on("/capture", handleCapture);
  server.begin();
}

void loop() {
  server.handleClient();
  if (scanRequested) executeScanAndReply();
  if (calibrateRequest > 0) { performCalibration(calibrateRequest); calibrateRequest = 0; }
}
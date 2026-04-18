#ifndef TABLERO_H
#define TABLERO_H
#include <EEPROM.h>
#define POSITIONS 9
#define COORDS 3
inline int calibData[POSITIONS][COORDS]; 
inline void initCalibrations() {
    for (int i=0; i<POSITIONS; i++) for (int j=0; j<COORDS; j++) {
        int val = EEPROM.read(i*COORDS+j); if(val>100) val=0; calibData[i][j]=val;
    }
}
inline int getCalibValue(int pos, int axis) { return calibData[pos][axis]; }
inline void setCalibValue(int pos, int axis, int val) { calibData[pos][axis]=val; }
inline void saveAllCalibrations() { for(int i=0; i<POSITIONS; i++) for(int j=0; j<COORDS; j++) EEPROM.write(i*COORDS+j, calibData[i][j]); EEPROM.commit(); }
void drawCalibrateTablero();
#endif
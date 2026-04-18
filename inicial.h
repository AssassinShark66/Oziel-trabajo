#ifndef INICIAL_H
#define INICIAL_H
#include <EEPROM.h>
#define EEPROM_SIZE 255
#define INIT_COORDS 3 
inline int initPos[INIT_COORDS]; 
inline void initInitialPos() {
    for (int i=0; i<INIT_COORDS; i++) { int val = EEPROM.read(90 + i); if(val>100) val=0; initPos[i]=val; }
}
inline int getInitValue(int axis) { return initPos[axis]; }
inline void setInitValue(int axis, int val) { initPos[axis] = val; }
inline void saveInitialPos() { for (int i=0; i<INIT_COORDS; i++) EEPROM.write(90+i, initPos[i]); EEPROM.commit(); }
void drawCalibrateInicial();
#endif
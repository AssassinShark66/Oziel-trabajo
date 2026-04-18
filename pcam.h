#ifndef PCAM_H
#define PCAM_H
#include <EEPROM.h>
#define PCAM_COORDS 3   
inline int pcamPos[PCAM_COORDS];
inline void initPCamPos() {
    for (int i=0; i<PCAM_COORDS; i++) { int val = EEPROM.read(70+i); if(val>100) val=0; pcamPos[i]=val; }
}
inline int getPCamValue(int axis) { return pcamPos[axis]; }
inline void setPCamValue(int axis, int val) { pcamPos[axis]=val; }
inline void savePCamPos() { for (int i=0; i<PCAM_COORDS; i++) EEPROM.write(70+i, pcamPos[i]); EEPROM.commit(); }
void drawCalibratePCam();
#endif
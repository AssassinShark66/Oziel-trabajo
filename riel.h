#ifndef RIEL_H
#define RIEL_H
#include <EEPROM.h>
#define EEPROM_RIEL_ADDR 100     
#define RIEL_POSITIONS 5
#define RIEL_COORDS 3
inline int rielData[RIEL_POSITIONS][RIEL_COORDS]; 
inline void initRielPos() {
    for (int i=0; i<RIEL_POSITIONS; i++) for (int j=0; j<RIEL_COORDS; j++) {
            int val = EEPROM.read(EEPROM_RIEL_ADDR + i*RIEL_COORDS + j);
            if(val>100) val=0; rielData[i][j]=val;
    }
}
inline int getRielValue(int pos, int axis) { return rielData[pos][axis]; }
inline void setRielValue(int pos, int axis, int val) { rielData[pos][axis]=val; }
inline void saveRielPos() { for(int i=0; i<RIEL_POSITIONS; i++) for(int j=0; j<RIEL_COORDS; j++) EEPROM.write(EEPROM_RIEL_ADDR + i*RIEL_COORDS + j, rielData[i][j]); EEPROM.commit(); }
void drawCalibrateRiel(); 
#endif
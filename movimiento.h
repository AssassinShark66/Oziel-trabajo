#ifndef MOVIMIENTO_H
#define MOVIMIENTO_H

#include <ESP32Servo.h>
#include <AccelStepper.h> 
#include <EEPROM.h>

// --- DEFINICIÓN DE PINES ---
#define STEP_PIN    25
#define DIR_PIN     26
#define ENABLE_PIN  14  
#define MAGNET_PIN  27  
#define SERVO_Y_PIN 32
#define SERVO_Z_PIN 33

#define SWITCH_0_PIN   34
#define SWITCH_MAX_PIN 35

// --- DIRECCIONES EEPROM ---
#define EEPROM_ADDR_MAX_STEPS   200
#define EEPROM_ADDR_MOTOR_SPEED 204

// --- VARIABLES GLOBALES ---
inline long max_pasos_x = 1000; 
inline int motor_speed_percent = 100;
inline int currentZVal = 0; 

// 1. NUEVO: Variable para gestionar tiempos sin delay() en máquinas de estados externas
inline unsigned long moveTimer = 0;

inline Servo servoY; 
inline Servo servoZ;
inline AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);

// --- FUNCIONES DE CONFIGURACIÓN ---

inline void loadStepperConfig() {
    EEPROM.get(EEPROM_ADDR_MAX_STEPS, max_pasos_x);
    EEPROM.get(EEPROM_ADDR_MOTOR_SPEED, motor_speed_percent);
    // Valores por defecto si la EEPROM está vacía o corrupta
    if (max_pasos_x <= 0 || max_pasos_x > 500000) max_pasos_x = 2000;
    if (motor_speed_percent < 0 || motor_speed_percent > 100) motor_speed_percent = 100;
}

inline void saveStepperConfig() {
    EEPROM.put(EEPROM_ADDR_MAX_STEPS, max_pasos_x);
    EEPROM.put(EEPROM_ADDR_MOTOR_SPEED, motor_speed_percent);
    EEPROM.commit();
}

inline void setMotorSpeedPercent(int percent) {
    if (percent < 0) percent = 0; if (percent > 100) percent = 100;
    motor_speed_percent = percent;
    // Mapeo de porcentaje a pasos/segundo y aceleración
    float new_speed = map(percent, 0, 100, 100, 1500); 
    float new_accel = map(percent, 0, 100, 50, 800);
    stepper.setMaxSpeed(new_speed); 
    stepper.setAcceleration(new_accel);
}

inline int getMotorSpeedPercent() { return motor_speed_percent; }
inline void setMagnet(bool state) { digitalWrite(MAGNET_PIN, state ? HIGH : LOW); }

inline void initMovimiento() {
    pinMode(SWITCH_0_PIN, INPUT); 
    pinMode(SWITCH_MAX_PIN, INPUT);
    pinMode(ENABLE_PIN, OUTPUT); digitalWrite(ENABLE_PIN, LOW); 
    pinMode(MAGNET_PIN, OUTPUT); digitalWrite(MAGNET_PIN, LOW); 
    
    stepper.setEnablePin(ENABLE_PIN); 
    stepper.setPinsInverted(false, false, true); 
    
    loadStepperConfig(); 
    setMotorSpeedPercent(motor_speed_percent); 
    stepper.setCurrentPosition(0); 
    
    servoY.setPeriodHertz(50); servoZ.setPeriodHertz(50); 
    servoY.attach(SERVO_Y_PIN, 500, 2400); servoZ.attach(SERVO_Z_PIN, 500, 2400);
    // Inicializar servos en posición segura
    servoZ.write(map(0, 0, 100, 0, 180)); currentZVal = 0;
}

// 2. NUEVA FUNCIÓN: Checa si el motor sigue trabajando (sin bloquear)
inline bool isMotorRunning() {
    return (stepper.distanceToGo() != 0);
}

// 3. MODIFICADA: Mueve ejes sin 'while' para el motor a pasos
inline void moverEje(int axis, int val) {
    if (val < 0) val = 0; if (val > 100) val = 100;
    
    // Lógica de seguridad del Eje Z (Muñeca)
    // Si vamos a mover X o Y, y la muñeca está abajo, la subimos primero.
    // Mantenemos este delay pequeño (300ms) por simplicidad de seguridad,
    // pero el movimiento largo del motor a pasos ya NO bloquea.
    if (axis == 0 || axis == 1) { 
        if (currentZVal > 5) { 
            servoZ.write(map(0, 0, 100, 0, 180)); 
            currentZVal = 0; 
            delay(300); // Pequeña pausa de seguridad
        } 
    }

    if (axis == 0) { 
        stepper.enableOutputs(); 
        // Solo asignamos el destino. EL MOTOR NO SE MOVERÁ A MENOS QUE
        // LLAMES A stepper.run() EN TU VOID LOOP() PRINCIPAL.
        stepper.moveTo(map(val, 0, 100, 0, max_pasos_x)); 
    } 
    else if (axis == 1) { servoY.write(map(val, 0, 100, 0, 180)); } 
    else if (axis == 2) { servoZ.write(map(val, 0, 100, 0, 180)); currentZVal = val; }
}

// --- CALIBRACIÓN DE CARRERA (Homing) ---
// Esta función se mantiene bloqueante porque es un proceso de configuración
// donde el usuario espera y no hay juego activo.
inline long runFullCalibration() {
    stepper.enableOutputs(); 
    
    // 1. Configurar velocidad muy lenta y aceleración alta (frenado seco)
    stepper.setMaxSpeed(200.0);      // Lento para precisión
    stepper.setAcceleration(1000.0); // Frenado rápido

    // 2. BUSCAR CERO (Moverse Atrás)
    stepper.move(-300000); 
    
    while (digitalRead(SWITCH_0_PIN) == HIGH) {
        stepper.run();
    }
    
    // STOP INMEDIATO
    stepper.setCurrentPosition(0); 
    stepper.runToPosition(); 
    
    delay(200);
    
    // Moverse un poco adelante para soltar el switch
    stepper.runToNewPosition(100);

    // 3. BUSCAR MAX (Moverse Adelante)
    stepper.move(300000); 
    
    while (digitalRead(SWITCH_MAX_PIN) == HIGH) {
        stepper.run();
    }
    
    // STOP INMEDIATO
    long finalPos = stepper.currentPosition();
    stepper.setCurrentPosition(finalPos);
    stepper.runToPosition();
    
    // 4. GUARDAR Y SALIR
    max_pasos_x = finalPos; 
    saveStepperConfig();

    // Restaurar velocidad normal
    setMotorSpeedPercent(motor_speed_percent);
    
    // Regresar a 0
    stepper.moveTo(0);
    while (stepper.distanceToGo() != 0) { // Bloqueante solo aquí para regresar seguro
        stepper.run();
    }

    stepper.disableOutputs(); 
    return max_pasos_x; 
}

// Dummy para evitar errores de compilación cruzada
void drawCalibrateStepper(); 
#endif
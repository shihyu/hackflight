#pragma once

#define BARO_TAB_SIZE_MAX   48

#include "baro.h"

void initSensors(sensor_t * gyro, bool * baro_available, bool * sonar_available);

void ACC_getADC(void);
void Gyro_getADC(sensor_t * gyro);
bool initBaro(baro_t * baro);
int Baro_update(void);
bool initSonar();
void Sonar_update(int32_t * SonarAlt);



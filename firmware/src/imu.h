#pragma once
#include <stdint.h>

// Minimal QMI8658C accelerometer reader -- enough to tell which way up the cube
// is (see orientation.h). The gyro and the interrupt pin are not used.

// Call after touch.begin(), which starts the shared I2C bus. False if the sensor
// does not answer; the cube then simply never auto-rotates.
bool imuBegin();

// Acceleration in g along the sensor's x, y and z axes. False on an I2C error.
bool imuReadAccel(float &ax, float &ay, float &az);

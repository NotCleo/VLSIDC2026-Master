#ifndef SENSOR_H
#define SENSOR_H

// Setup the GPIO pins (Export, Direction, Open Files)
// Returns 0 on success, -1 on failure
int sensor_init(void);

// triggers the sensor and returns distance in CM
// Returns -1.0 if the reading failed/timed out
double sensor_get_distance(void);

// Closes files and cleans up 
void sensor_cleanup(void);

#endif
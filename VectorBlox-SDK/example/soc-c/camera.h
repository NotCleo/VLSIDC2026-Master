#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>

// 1. Setup the camera, map memory, and start the stream (do this once at startup)
// Returns 0 on success, -1 on error
int camera_init(void);

// 2. Grabs the current frame, converts to RGB, and saves to filename
// Input: filename (e.g., "box_1.jpg")
// Output: 0 on success, -1 on error
int camera_capture_to_file(const char *filename);

// 3. (Optional) If you want to analyze the buffer in memory directly later
// Returns a pointer to the RGB pixel array
uint8_t* camera_get_last_frame_ptr(void);

// 4. Clean up resources
void camera_cleanup(void);

#endif
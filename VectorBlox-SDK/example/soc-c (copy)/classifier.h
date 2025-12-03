#ifndef CLASSIFIER_H
#define CLASSIFIER_H

#ifdef __cplusplus
extern "C" {
#endif

// 1. Initialize the AI Accelerator and load the model
// Returns 0 on success, -1 on failure
int classifier_init(const char* model_filename);

// 2. Run inference on a JPG file
// Returns the Class ID (0, 1, 2...) of the detected object
// Returns -1 on error
int classifier_predict(const char* jpg_filename);

// 3. Cleanup memory
void classifier_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
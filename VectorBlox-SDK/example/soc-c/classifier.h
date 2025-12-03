#ifndef CLASSIFIER_H
#define CLASSIFIER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the VectorBlox CNN accelerator and loads the model.
 * * @param model_filename Path to the compiled .vnnx model file.
 * @return 0 on success, -1 on failure.
 */
int classifier_init(const char *model_filename);

/**
 * @brief Runs inference on a JPEG image.
 * * @param image_filename Path to the .jpg image file.
 * @return The predicted Class ID (0, 1, 2...) on success, -1 on failure.
 */
int classifier_predict(const char *image_filename);

/**
 * @brief Cleans up resources (optional).
 */
void classifier_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
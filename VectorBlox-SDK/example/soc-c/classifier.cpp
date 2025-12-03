#include "classifier.h"
#include "postprocess.h"
#include "vbx_cnn_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "pdma/pdma_helpers.h"
#include <cassert>
#include <limits.h>

// --- External Helper Declarations ---
extern "C" int read_JPEG_file (const char * filename, int* width, int* height,
        unsigned char **image, const int grayscale);
extern "C" int resize_image(uint8_t *image_in, int in_w, int in_h,
        uint8_t *image_out, int out_w, int out_h);

// --- Constants & Globals ---
#define TFLITE 1
#ifndef USE_INTERRUPTS
#define USE_INTERRUPTS 1
#endif

// Global state variables
static vbx_cnn_t *vbx_cnn = NULL;
static model_t *model = NULL;
static int8_t* pdma_mmap_ptr = NULL;
static uint64_t pdma_phys_base = 0;
static int32_t pdma_channel = -1;
static vbx_cnn_io_ptr_t io_buffers[MAX_IO_BUFFERS];
static int is_initialized = 0;

// --- Internal Helper Functions ---

static inline void* virt_to_phys(vbx_cnn_t* vbx_cnn, void* virt){
    return (char*)(virt) + vbx_cnn->dma_phys_trans_offset;
}

#if USE_INTERRUPTS
static void enable_interrupt(vbx_cnn_t *vbx_cnn){
    uint32_t reenable = 1;
    ssize_t writeSize = write(vbx_cnn->fd, &reenable, sizeof(uint32_t));
    if(writeSize < 0) {
        close(vbx_cnn->fd);
    }
}
#endif

static uint64_t internal_pdma_mmap(int total_size){
    char cdev[256] = "/dev/udmabuf-ddr-nc0";
    uint64_t ddrc_phyadr = get_phy_addr(cdev);  
    int32_t fdc = open(cdev, O_RDWR); 
    off_t oft = 0;
    pdma_mmap_ptr = (int8_t *)mmap(NULL, total_size*sizeof(int8_t), PROT_READ | PROT_WRITE,  MAP_SHARED, fdc, oft);
    assert(pdma_mmap_ptr != NULL);
    return ddrc_phyadr + oft;
}

static int32_t internal_pdma_ch_transfer(uint64_t output_data_phys, void* source_buffer, int offset, int size, vbx_cnn_t *vbx_cnn, int32_t channel){
    uint64_t srcbuf = (uint64_t)(uintptr_t)virt_to_phys(vbx_cnn, source_buffer);
    return pdma_ch_cpy(output_data_phys + offset, srcbuf, size, channel);
}

static void* read_and_resize_image(const char* filename, const int channels, const int height, const int width, int use_bgr){
    unsigned char* image = NULL;
    int h, w;
    
    // --- CRITICAL FIX ---
    // We call the function but ignore the integer return code 'ret'.
    // We only check if 'image' was populated.
    int ret = read_JPEG_file(filename, &w, &h, &image, channels == 1);
    
    // Only fail if the pointer is NULL. Ignore warnings (ret=1)
    if (!image) {
        fprintf(stderr, "read_JPEG_file returned %d and image pointer is NULL.\n", ret);
        return NULL;
    }

    // If we get here, libjpeg might have complained (ret=1), but it gave us pixels!
    
    unsigned char* planer_img = (unsigned char*)malloc(w*h*channels);
    for(int r=0; r<h; r++){
        for(int c=0; c<w; c++){
            for(int ch=0; ch<channels; ch++){
                if (use_bgr) {
                    planer_img[ch*w*h+r*w+c] = image[(r*w+c)*channels+((channels-1)-ch)];
                } else {
                    planer_img[ch*w*h+r*w+c] = image[(r*w+c)*channels+ch];
                }
            }
        }
    }
    free(image);

    unsigned char* resized_planar_img = (unsigned char*)malloc(width*height*channels);
    for(int ch=0; ch<channels; ch++){
        resize_image((uint8_t*)planer_img + ch*w*h, w, h,
                (uint8_t*)resized_planar_img + ch*width*height, width, height);
    }
    free(planer_img);
    return resized_planar_img;
}

static model_t *internal_read_model_file(vbx_cnn_t *vbx_cnn, const char *filename) {
    FILE *model_file = fopen(filename, "r");
    if (model_file == NULL) return NULL;

    fseek(model_file, 0, SEEK_END);
    int file_size = ftell(model_file);
    fseek(model_file, 0, SEEK_SET);

    model_t *temp_model = (model_t *)malloc(file_size);
    int size_read = fread(temp_model, 1, file_size, model_file);
    fclose(model_file);

    if (size_read != file_size) {
        free(temp_model);
        return NULL;
    }

    int model_data_size = model_get_data_bytes(temp_model);
    int model_allocate_size = model_get_allocate_bytes(temp_model);
    
    if (model_allocate_size < model_data_size) {
         free(temp_model);
         return NULL;
    }

    temp_model = (model_t *)realloc(temp_model, model_allocate_size);
    model_t *dma_model = (model_t *)vbx_allocate_dma_buffer(vbx_cnn, model_allocate_size, 0);
    
    if (dma_model) {
        memcpy(dma_model, temp_model, model_data_size);
    }
    free(temp_model);
    return dma_model;
}


// --- Public API Implementation ---

int classifier_init(const char *model_filename) {
    if (is_initialized) return 0;

    vbx_cnn = vbx_cnn_init(NULL);
    if (!vbx_cnn) {
        fprintf(stderr, "Error: Unable to initialize vbx_cnn\n");
        return -1;
    }

    model = internal_read_model_file(vbx_cnn, model_filename);
    if (!model) {
        fprintf(stderr, "Error: Unable to read model %s\n", model_filename);
        return -1;
    }

    if (model_check_sanity(model) != 0) {
        fprintf(stderr, "Error: Model is not sane\n");
        return -1;
    }

    // Setup PDMA
    int total_size = 32*1024*1024; 
    pdma_phys_base = internal_pdma_mmap(total_size);
    pdma_channel = pdma_ch_open();

    // Allocate Input Buffers
    for(unsigned i = 0; i < model_get_num_inputs(model); ++i){
        io_buffers[i] = (vbx_cnn_io_ptr_t)vbx_allocate_dma_buffer(vbx_cnn, model_get_input_length(model,i)*sizeof(uint8_t), 1);
        if(!io_buffers[i]){
            fprintf(stderr, "Error: Input buffer allocation failed\n");
            return -1;
        }
    }

    // Allocate Output Buffers
    for (unsigned o = 0; o < model_get_num_outputs(model); ++o) {
        io_buffers[model_get_num_inputs(model) + o] = (vbx_cnn_io_ptr_t)vbx_allocate_dma_buffer(
                vbx_cnn, model_get_output_length(model, o) * sizeof(uint32_t), 0);
        if(!io_buffers[model_get_num_inputs(model) + o]){
             fprintf(stderr, "Error: Output buffer allocation failed\n");
             return -1;
        }
    }

#if USE_INTERRUPTS
    enable_interrupt(vbx_cnn);
#endif

    is_initialized = 1;
    return 0;
}

int classifier_predict(const char *image_filename) {
    if (!is_initialized) {
        fprintf(stderr, "Error: Classifier not initialized\n");
        return -1;
    }

    // 1. Load and Resize Image
    int input_idx = 0; 
    int dims = model_get_input_dims(model, input_idx);
    int* input_shape = model_get_input_shape(model, input_idx);
    int input_length = model_get_input_length(model, input_idx);
    
    int h = input_shape[dims-2];
    int w = input_shape[dims-1];
    
    void* read_buffer = read_and_resize_image(image_filename, 3, h, w, 0); // 0 = RGB
    if (!read_buffer) {
        // Only error if pointer is NULL
        fprintf(stderr, "Error: Failed to read/resize image %s\n", image_filename);
        return -1;
    }

    // Copy to DMA buffer
    memcpy((void*)io_buffers[input_idx], read_buffer, input_length);
    free(read_buffer);

    // 2. Run Inference
    int status = vbx_cnn_model_start(vbx_cnn, model, io_buffers);
#if USE_INTERRUPTS
    status = vbx_cnn_model_wfi(vbx_cnn);
#else
    while(vbx_cnn_model_poll(vbx_cnn) > 0);
#endif

    if (status < 0) {
        fprintf(stderr, "Model failed with error %d\n", vbx_cnn_get_error_val(vbx_cnn));
        return -1;
    }

    // 3. Process Output
    int output_idx = 0; 
    int out_len = model_get_output_length(model, output_idx);
    fix16_t scale = (fix16_t)model_get_output_scale_fix16_value(model, output_idx);
    int32_t zero_point = model_get_output_zeropoint(model, output_idx);
    
    // Sync PDMA
    internal_pdma_ch_transfer(pdma_phys_base, (void*)io_buffers[model_get_num_inputs(model)+output_idx], 0, out_len, vbx_cnn, pdma_channel);

    int8_t* raw_output = (int8_t*)pdma_mmap_ptr;
    
    // Find ArgMax
    int max_index = -1;
    fix16_t max_val = -2147483648; 

    for (int i = 0; i < out_len; i++) {
        fix16_t val = (raw_output[i] - zero_point) * (float)scale / 65536.0f;
        if (val > max_val) {
            max_val = val;
            max_index = i;
        }
    }

    return max_index;
}

void classifier_cleanup() {
    is_initialized = 0;
}
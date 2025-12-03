#include "classifier.h"
#include "postprocess.h"
#include "vbx_cnn_api.h"
#include "pdma/pdma_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <vector>
#include <algorithm>

// --- External Helper Functions (Assumed to exist in your SDK) ---
extern "C" int read_JPEG_file (const char * filename, int* width, int* height, unsigned char **image, const int grayscale);
extern "C" int resize_image(uint8_t *image_in, int in_w, int in_h, uint8_t *image_out, int out_w, int out_h);

// --- Global State (Persists between calls) ---
static vbx_cnn_t *g_vbx_cnn = NULL;
static model_t *g_model = NULL;
static vbx_cnn_io_ptr_t g_io_buffers[MAX_IO_BUFFERS];
static int8_t* g_pdma_mmap_t = NULL;
static uint64_t g_pdma_phys_addr = 0;
static int32_t g_pdma_channel = -1;

// --- Helper: Memory Mapping ---
static uint64_t setup_pdma_memory(int total_size) {
    char cdev[256] = "/dev/udmabuf-ddr-nc0";
    uint64_t ddrc_phyadr = get_phy_addr(cdev);  
    int32_t fdc = open(cdev, O_RDWR); 
    if (fdc < 0) return 0;
    
    g_pdma_mmap_t = (int8_t *)mmap(NULL, total_size*sizeof(int8_t), PROT_READ | PROT_WRITE, MAP_SHARED, fdc, 0);
    return ddrc_phyadr; // Offset is 0
}

static void* virt_to_phys(vbx_cnn_t* vbx_cnn, void* virt){
    return (char*)(virt) + vbx_cnn->dma_phys_trans_offset;
}

static int32_t pdma_ch_transfer_wrapper(uint64_t output_data_phys, void* source_buffer, int offset, int size, vbx_cnn_t *vbx_cnn, int32_t channel){
    uint64_t srcbuf = (uint64_t)(uintptr_t)virt_to_phys(vbx_cnn, source_buffer);
    return pdma_ch_cpy(output_data_phys + offset, srcbuf, size, channel);
}

// --- Helper: Read Image ---
static void* internal_read_image(const char* filename, int channels, int height, int width, int data_type, int use_bgr){
    unsigned char* image;
    int h, w;
    if (read_JPEG_file(filename, &w, &h, &image, channels == 1) != 0) return NULL;

    unsigned char* planer_img = (unsigned char*)malloc(w*h*channels);
    for(int r=0; r<h; r++){
        for(int c=0; c<w; c++){
            for(int ch=0; ch<channels; ch++){
                if (use_bgr) planer_img[ch*w*h+r*w+c] = image[(r*w+c)*channels+((channels-1)-ch)];
                else         planer_img[ch*w*h+r*w+c] = image[(r*w+c)*channels+ch];
            }
        }
    }
    free(image);

    unsigned char* resized = (unsigned char*)malloc(width*height*channels);
    for(int ch=0; ch<channels; ch++){
        resize_image((uint8_t*)planer_img + ch*w*h, w, h,
                     (uint8_t*)resized + ch*width*height, width, height);
    }
    free(planer_img);
    return resized;
}

// --- PUBLIC API ---

int classifier_init(const char* model_filename) {
    // 1. Init Hardware
    g_vbx_cnn = vbx_cnn_init(NULL);
    if (!g_vbx_cnn) { fprintf(stderr, "VBX Init Failed\n"); return -1; }

    // 2. Enable Interrupts
    uint32_t reenable = 1;
    write(g_vbx_cnn->fd, &reenable, sizeof(uint32_t));

    // 3. Load Model
    FILE *f = fopen(model_filename, "r");
    if (!f) { fprintf(stderr, "Model file not found: %s\n", model_filename); return -1; }
    fseek(f, 0, SEEK_END);
    int fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    model_t *raw_model = (model_t *)malloc(fsize);
    fread(raw_model, 1, fsize, f);
    fclose(f);

    int alloc_size = model_get_allocate_bytes(raw_model);
    g_model = (model_t *)vbx_allocate_dma_buffer(g_vbx_cnn, alloc_size, 0);
    memcpy(g_model, raw_model, model_get_data_bytes(raw_model));
    free(raw_model);

    if (model_check_sanity(g_model) != 0) { fprintf(stderr, "Model Insane\n"); return -1; }

    // 4. Setup IO Buffers (Input side only here)
    for(unsigned i=0; i<model_get_num_inputs(g_model); ++i){
        int len = model_get_input_length(g_model, i);
        g_io_buffers[i] = (vbx_cnn_io_ptr_t)vbx_allocate_dma_buffer(g_vbx_cnn, len*sizeof(uint8_t), 1);
    }

    // 5. Setup Output Buffers
    for (unsigned o = 0; o < model_get_num_outputs(g_model); ++o) {
        int len = model_get_output_length(g_model, o);
        g_io_buffers[model_get_num_inputs(g_model) + o] = 
            (vbx_cnn_io_ptr_t)vbx_allocate_dma_buffer(g_vbx_cnn, len * sizeof(uint32_t), 0);
    }

    // 6. Setup PDMA (for output reading)
    g_pdma_phys_addr = setup_pdma_memory(32*1024*1024);
    g_pdma_channel = pdma_ch_open();

    printf("Classifier Initialized.\n");
    return 0;
}

int classifier_predict(const char* jpg_filename) {
    if (!g_model || !g_vbx_cnn) return -1;

    // 1. Load and Preprocess Image
    int dims = model_get_input_dims(g_model, 0);
    int* shape = model_get_input_shape(g_model, 0);
    int h = shape[dims-2];
    int w = shape[dims-1];
    
    void* img_data = internal_read_image(jpg_filename, 3, h, w, 0, 0);
    if (!img_data) { fprintf(stderr, "Failed to read image\n"); return -1; }

    // Copy to DMA buffer (Input 0)
    memcpy((void*)g_io_buffers[0], img_data, model_get_input_length(g_model, 0));
    free(img_data);

    // 2. Run Inference
    int status = vbx_cnn_model_start(g_vbx_cnn, g_model, g_io_buffers);
    status = vbx_cnn_model_wfi(g_vbx_cnn); // Wait for interrupt
    if (status < 0) return -1;

    // 3. Process Outputs (Fix16 conversion)
    // Assuming Output 0 is the classification vector
    int out_idx = 0; 
    int size = model_get_output_length(g_model, out_idx);
    fix16_t scale = (fix16_t)model_get_output_scale_fix16_value(g_model, out_idx);
    int32_t zero_point = model_get_output_zeropoint(g_model, out_idx);
    
    // Transfer from Accelerator memory to CPU accessible memory
    pdma_ch_transfer_wrapper(g_pdma_phys_addr, (void*)g_io_buffers[model_get_num_inputs(g_model)+out_idx], 
                             0, size, g_vbx_cnn, g_pdma_channel);

    // Now read from g_pdma_mmap_t
    int8_t* raw_output = (int8_t*)g_pdma_mmap_t;
    
    // Find Max (Argmax)
    int best_class = -1;
    float max_score = -99999.0;

    for (int i=0; i<size; i++) {
        // Convert int8 to float for comparison (simplified fix16 logic)
        float val = (raw_output[i] - zero_point) * (float)scale / 65536.0f; 
        if (val > max_score) {
            max_score = val;
            best_class = i;
        }
    }

    return best_class;
}

void classifier_cleanup() {
    // Free buffers if needed (omitted for brevity, usually OS handles cleanup on exit)
}
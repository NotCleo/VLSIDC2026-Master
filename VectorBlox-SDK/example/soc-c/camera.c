#include "camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// --- Configuration ---
#define WIDTH 320
#define HEIGHT 240
#define QUALITY 90 
#define DEVICE_PATH "/dev/video0"

// --- Internal Global Variables ---
static int cam_fd = -1;
static void *buffer_start = NULL;
static struct v4l2_buffer buf = {0};
static uint8_t *rgb_buffer = NULL; // We will reuse this memory

// --- Internal Helper Functions ---

static inline uint8_t clamp(int v) {
    return (v < 0) ? 0 : ((v > 255) ? 255 : (uint8_t)v);
}

// Helper: Wrapper for ioctl to handle retries
static int xioctl(int fh, int request, void *arg) {
    int r;
    do { r = ioctl(fh, request, arg); } while (-1 == r && EINTR == errno);
    return r;
}

// Helper: YUYV to RGB Conversion
static void yuyv_to_rgb(uint8_t *yuyv, uint8_t *rgb, int width, int height) {
    int i, j, y0, u, y1, v, r, g, b;
    int pixel_count = width * height;
    
    for (i = 0, j = 0; i < pixel_count * 2; i += 4, j += 6) {
        y0 = yuyv[i];
        u  = yuyv[i + 1] - 128;
        y1 = yuyv[i + 2];
        v  = yuyv[i + 3] - 128;

        // Pixel 1
        r = y0 + (1.402 * v);
        g = y0 - (0.344136 * u) - (0.714136 * v);
        b = y0 + (1.772 * u);
        rgb[j] = clamp(r); rgb[j+1] = clamp(g); rgb[j+2] = clamp(b);

        // Pixel 2
        r = y1 + (1.402 * v);
        g = y1 - (0.344136 * u) - (0.714136 * v);
        b = y1 + (1.772 * u);
        rgb[j+3] = clamp(r); rgb[j+4] = clamp(g); rgb[j+5] = clamp(b);
    }
}

// --- Public Functions ---

int camera_init(void) {
    struct v4l2_format fmt = {0};
    struct v4l2_requestbuffers req = {0};
    enum v4l2_buf_type type;

    // 1. Open Camera
    cam_fd = open(DEVICE_PATH, O_RDWR | O_NONBLOCK);
    if (cam_fd < 0) { perror("Cam Open"); return -1; }

    // 2. Set Format
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(cam_fd, VIDIOC_S_FMT, &fmt) < 0) { perror("Set Format"); return -1; }

    // 3. Request Memory
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(cam_fd, VIDIOC_REQBUFS, &req) < 0) { perror("Req Buffer"); return -1; }

    // 4. Map Memory
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if (xioctl(cam_fd, VIDIOC_QUERYBUF, &buf) < 0) { perror("Query Buffer"); return -1; }

    buffer_start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, cam_fd, buf.m.offset);
    if (buffer_start == MAP_FAILED) { perror("Mmap"); return -1; }

    // 5. Start Stream
    if (xioctl(cam_fd, VIDIOC_QBUF, &buf) < 0) { perror("Queue Buffer"); return -1; }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(cam_fd, VIDIOC_STREAMON, &type) < 0) { perror("Stream On"); return -1; }

    // 6. Pre-allocate RGB buffer
    rgb_buffer = malloc(WIDTH * HEIGHT * 3);
    
    // 7. Warm Up (Do this ONCE at startup)
    printf("Camera warming up...\n");
    fd_set fds;
    struct timeval tv;
    for(int i=0; i<10; i++) {
        FD_ZERO(&fds); FD_SET(cam_fd, &fds);
        tv.tv_sec = 2; tv.tv_usec = 0;
        select(cam_fd + 1, &fds, NULL, NULL, &tv);
        xioctl(cam_fd, VIDIOC_DQBUF, &buf); // Dequeue
        xioctl(cam_fd, VIDIOC_QBUF, &buf);  // Requeue
    }
    printf("Camera Ready.\n");
    return 0;
}

int camera_capture_to_file(const char *filename) {
    if (cam_fd == -1) return -1;

    fd_set fds;
    struct timeval tv;
    int r;

    // 1. Wait for frame
    FD_ZERO(&fds); FD_SET(cam_fd, &fds);
    tv.tv_sec = 2; tv.tv_usec = 0;
    r = select(cam_fd + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) return -1;

    // 2. Grab Frame (Dequeue)
    if (xioctl(cam_fd, VIDIOC_DQBUF, &buf) < 0) return -1;

    // 3. Process (Raw -> RGB)
    yuyv_to_rgb((uint8_t*)buffer_start, rgb_buffer, WIDTH, HEIGHT);

    // 4. Save to Disk
    if (stbi_write_jpg(filename, WIDTH, HEIGHT, 3, rgb_buffer, QUALITY)) {
        printf("Saved: %s\n", filename);
    } else {
        printf("Failed to save image.\n");
    }

    // 5. Put Buffer Back (Requeue) for next time!
    if (xioctl(cam_fd, VIDIOC_QBUF, &buf) < 0) return -1;

    return 0;
}

uint8_t* camera_get_last_frame_ptr(void) {
    return rgb_buffer;
}

void camera_cleanup(void) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(cam_fd, VIDIOC_STREAMOFF, &type);
    if (buffer_start) munmap(buffer_start, buf.length);
    if (cam_fd != -1) close(cam_fd);
    if (rgb_buffer) free(rgb_buffer);
}
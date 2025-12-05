#include "ultrasonic.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>

// Configuration
#define GPIO_BASE 512
#define TRIG_OFFSET 5    
#define ECHO_OFFSET 15   
#define GPIO_PATH "/sys/class/gpio/"

// Internal Variables (Static means only visible in this file)
static char TRIG_PIN_STR[16];
static char ECHO_PIN_STR[16];
static int trig_fd = -1;
static int echo_fd = -1;

// Internal Helper Functions

static void setup_gpio_internal(const char *pin, const char *direction) {
    char path[128];
    char check_path[128];
    
    // 1. Check if exported, export if not
    snprintf(check_path, sizeof(check_path), "%sgpio%s/direction", GPIO_PATH, pin);
    if (access(check_path, F_OK) == -1) {
        snprintf(path, sizeof(path), "%sexport", GPIO_PATH);
        int fd = open(path, O_WRONLY);
        if (fd != -1) {
            write(fd, pin, strlen(pin));
            close(fd);
            usleep(100000); // Critical wait for sysfs to create files
        }
    }

    // 2. Set Direction
    int fd = open(check_path, O_WRONLY);
    if (fd != -1) {
        write(fd, direction, strlen(direction));
        close(fd);
    }
}

static int open_gpio_value(const char *pin) {
    char path[128];
    snprintf(path, sizeof(path), "%sgpio%s/value", GPIO_PATH, pin);
    return open(path, O_RDWR);
}

// Public Functions  

int sensor_init(void) {
    // Calculate string names
    sprintf(TRIG_PIN_STR, "%d", GPIO_BASE + TRIG_OFFSET);
    sprintf(ECHO_PIN_STR, "%d", GPIO_BASE + ECHO_OFFSET);

    // Setup Physical Pins
    setup_gpio_internal(TRIG_PIN_STR, "out");
    setup_gpio_internal(ECHO_PIN_STR, "in");

    // Open File Descriptors and store them persistently
    trig_fd = open_gpio_value(TRIG_PIN_STR);
    echo_fd = open_gpio_value(ECHO_PIN_STR);

    if (trig_fd == -1 || echo_fd == -1) {
        perror("Sensor Init Failed");
        return -1;
    }
    return 0;
}

double sensor_get_distance(void) {
    if (trig_fd == -1 || echo_fd == -1) return -1.0;

    char buffer[2];
    struct timeval start, end;

    // 1. TRIGGER PULSE (10us)
    write(trig_fd, "1", 1);
    usleep(10);
    write(trig_fd, "0", 1);

    // 2. WAIT FOR ECHO START (0 -> 1)
    long timeout = 0;
    int signal_started = 0;
    
    while (timeout < 50000) { 
        lseek(echo_fd, 0, SEEK_SET); // Rewind
        read(echo_fd, buffer, 1);
        if (buffer[0] == '1') {
            gettimeofday(&start, NULL);
            signal_started = 1;
            break;
        }
        timeout++;
    }

    if (!signal_started) return -1.0; // Timeout

    // 3. WAIT FOR ECHO END (1 -> 0)
    timeout = 0;
    int signal_ended = 0;
    while (timeout < 50000) {
        lseek(echo_fd, 0, SEEK_SET); // Rewind
        read(echo_fd, buffer, 1);
        if (buffer[0] == '0') {
            gettimeofday(&end, NULL);
            signal_ended = 1;
            break;
        }
        timeout++;
    }

    if (!signal_ended) return -1.0; // Timeout

    // 4. CALCULATE
    long seconds = end.tv_sec - start.tv_sec;
    long micros = ((seconds * 1000000) + end.tv_usec) - start.tv_usec;
    
    // Distance = (Time * Speed of Sound 0.0343 cm/us) / 2
    double distance = (micros * 0.0343) / 2.0;
    
    // Filter noise
    if (distance > 400 || distance < 2) return -1.0;

    return distance;
}

void sensor_cleanup(void) {
    if (trig_fd != -1) close(trig_fd);
    if (echo_fd != -1) close(echo_fd);
}
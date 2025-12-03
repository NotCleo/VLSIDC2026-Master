#include "servo.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

// ==========================================
// CONFIGURATION
// ==========================================
#define GPIO_BASE 512       // CHANGE THIS to your actual base
#define SERVO_PIN_OFFSET 12 // Pin 18 (Line 12)
#define GPIO_PATH "/sys/class/gpio/"

// MG995 Timing Constants (in microseconds)
#define PWM_PERIOD 20000     // 20ms period (50Hz)
#define PULSE_MIN 450        // 0 degrees
#define PULSE_MAX 2500       // 180 degrees

// Internal Global to store the calculated pin string
static char SERVO_PIN[16];

// --- Internal Helper Functions ---

// Generates PWM pulses to hold the servo at a specific angle
// This function blocks execution for duration_ms
static void hold_angle(int gpio_fd, int angle, int duration_ms) {
    if (gpio_fd < 0) return;

    // 1. Calculate pulse width
    // Map angle (0-180) to pulse (450-2500)
    int pulse_width = PULSE_MIN + ((angle * (PULSE_MAX - PULSE_MIN)) / 180);
    int sleep_time = PWM_PERIOD - pulse_width;

    // 2. Calculate how many loops to run
    // Each loop is 20ms. Duration / 20 = number of cycles.
    int cycles = duration_ms / 20;

    for (int i = 0; i < cycles; i++) {
        // High
        write(gpio_fd, "1", 1);
        usleep(pulse_width); 

        // Low
        write(gpio_fd, "0", 1);
        usleep(sleep_time);
    }
}

static void setup_gpio_internal(const char *pin) {
    char path[128], check_path[128];
    
    // Check/Export
    snprintf(check_path, sizeof(check_path), "%sgpio%s/direction", GPIO_PATH, pin);
    if (access(check_path, F_OK) == -1) {
        snprintf(path, sizeof(path), "%sexport", GPIO_PATH);
        int fd = open(path, O_WRONLY);
        if (fd == -1) { 
            perror("Servo: Error exporting GPIO"); 
            return; // Initialization will likely fail later
        }
        write(fd, pin, strlen(pin));
        close(fd);
        usleep(100000); // Wait for system to create files
    }

    // Set as Output
    int fd = open(check_path, O_WRONLY);
    if (fd == -1) { 
        perror("Servo: Error setting direction"); 
        return; 
    }
    write(fd, "out", 3);
    close(fd);
}

// --- Public API Implementation ---

int servo_init(void) {
    // Calculate Pin Name based on Base + Offset
    sprintf(SERVO_PIN, "%d", GPIO_BASE + SERVO_PIN_OFFSET);
    printf("Servo: Initializing on GPIO %s\n", SERVO_PIN);

    // Setup Export and Direction
    setup_gpio_internal(SERVO_PIN);

    // Open File Descriptor for Value
    char val_path[128];
    snprintf(val_path, sizeof(val_path), "%sgpio%s/value", GPIO_PATH, SERVO_PIN);
    
    int fd = open(val_path, O_WRONLY);
    if (fd == -1) { 
        perror("Servo: Error opening value file"); 
        return -1; 
    }

    return fd;
}

void servo_perform_cycle(int gpio_fd, int target_angle) {
    if (gpio_fd < 0) {
        printf("Servo: Invalid file descriptor\n");
        return;
    }

    // 1. Start at 0 (Initial Position)
    // We hold this briefly (500ms) to ensure it physically moves to start if not there
    printf("Servo: Moving to 0 degrees (Start)\n");
    hold_angle(gpio_fd, 0, 500); 

    // 2. Shift to set angle (e.g. 60) and hold for 3 seconds
    printf("Servo: Moving to %d degrees (Action)\n", target_angle);
    hold_angle(gpio_fd, target_angle, 3000); 

    // 3. Revert back to 0
    // Holding for 1 second to ensure it settles back
    printf("Servo: Reverting to 0 degrees (End)\n");
    hold_angle(gpio_fd, 0, 1000); 
}

void servo_close(int gpio_fd) {
    if (gpio_fd != -1) {
        close(gpio_fd);
        printf("Servo: Closed.\n");
    }
}

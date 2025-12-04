#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include "uart.h"
#include "pwm.h"
#include "ultrasonic.h"
#include "camera.h"
#include "classifier.h"
#include "servo.h"

// ==========================================
// CONFIGURATION
// ==========================================
#define PWM_CHANNEL 0
#define PWM_PERIOD_NS 20000000    // 20ms = 50Hz
#define PWM_DUTY_NS 1500000       // 1.5ms pulse width (adjust for your motor)
#define DISTANCE_THRESHOLD 8.0    // cm
#define MODEL_PATH "./model.vnnx" // Path to your compiled model
#define IMAGE_PATH "./capture.jpg"

// GPIO Configuration for Pin 22 (Line 13)
#define GPIO_BASE 512
#define DEFECT_PIN_OFFSET 13      // Line 13 corresponds to Pin 22
#define GPIO_PATH "/sys/class/gpio/"

// Servo angle for defect rejection
#define SERVO_REJECT_ANGLE 60

// ==========================================
// GLOBAL STATE
// ==========================================
static volatile int running = 1;
static int defect_gpio_fd = -1;

// ==========================================
// SIGNAL HANDLER
// ==========================================
void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    running = 0;
}

// ==========================================
// GPIO HELPER FUNCTIONS
// ==========================================
static void setup_defect_gpio(void) {
    char pin_str[16];
    char path[128];
    char check_path[128];
    
    sprintf(pin_str, "%d", GPIO_BASE + DEFECT_PIN_OFFSET);
    
    // Check if already exported
    snprintf(check_path, sizeof(check_path), "%sgpio%s/direction", GPIO_PATH, pin_str);
    if (access(check_path, F_OK) == -1) {
        // Export the pin
        snprintf(path, sizeof(path), "%sexport", GPIO_PATH);
        int fd = open(path, O_WRONLY);
        if (fd != -1) {
            write(fd, pin_str, strlen(pin_str));
            close(fd);
            usleep(100000); // Wait for sysfs
        }
    }
    
    // Set as output
    int fd = open(check_path, O_WRONLY);
    if (fd != -1) {
        write(fd, "out", 3);
        close(fd);
    }
    
    // Open value file
    snprintf(path, sizeof(path), "%sgpio%s/value", GPIO_PATH, pin_str);
    defect_gpio_fd = open(path, O_WRONLY);
    
    if (defect_gpio_fd == -1) {
        perror("Failed to open defect GPIO");
    } else {
        // Initialize to LOW
        write(defect_gpio_fd, "0", 1);
        printf("Defect GPIO (Pin 22, Line 13) initialized\n");
    }
}

static void set_defect_pin_high(void) {
    if (defect_gpio_fd != -1) {
        write(defect_gpio_fd, "1", 1);
        printf("Defect pin set HIGH\n");
    }
}

static void set_defect_pin_low(void) {
    if (defect_gpio_fd != -1) {
        write(defect_gpio_fd, "0", 1);
        printf("Defect pin set LOW\n");
    }
}

// ==========================================
// SYSTEM INITIALIZATION
// ==========================================
int initialize_system(void) {
    printf("=== System Initialization ===\n");
    
    // 1. Initialize UART
    if (uart_init() != 0) {
        fprintf(stderr, "ERROR: UART initialization failed\n");
        return -1;
    }
    printf("✓ UART initialized\n");
    
    // 2. Initialize Ultrasonic Sensor
    if (sensor_init() != 0) {
        fprintf(stderr, "ERROR: Ultrasonic sensor initialization failed\n");
        return -1;
    }
    printf("✓ Ultrasonic sensor initialized\n");
    
    // 3. Initialize Camera
    if (camera_init() != 0) {
        fprintf(stderr, "ERROR: Camera initialization failed\n");
        return -1;
    }
    printf("✓ Camera initialized\n");
    
    // 4. Initialize Classifier
    if (classifier_init(MODEL_PATH) != 0) {
        fprintf(stderr, "ERROR: Classifier initialization failed\n");
        return -1;
    }
    printf("✓ Classifier initialized\n");
    
    // 5. Setup Defect GPIO (Pin 22)
    setup_defect_gpio();
    
    printf("=== System Ready ===\n\n");
    return 0;
}

// ==========================================
// SYSTEM CLEANUP
// ==========================================
void cleanup_system(void) {
    printf("\n=== System Cleanup ===\n");
    
    // Stop PWM if running
    pwm_disable(PWM_CHANNEL);
    
    // Close UART
    uart_close();
    
    // Cleanup sensors
    sensor_cleanup();
    camera_cleanup();
    classifier_cleanup();
    
    // Close defect GPIO
    if (defect_gpio_fd != -1) {
        set_defect_pin_low();
        close(defect_gpio_fd);
    }
    
    printf("✓ System cleaned up\n");
}

// ==========================================
// INSPECTION CYCLE
// ==========================================
void run_inspection_cycle(int servo_fd) {
    printf("\n--- Starting Inspection Cycle ---\n");
    
    // 1. Start PWM (conveyor motor)
    printf("Starting PWM (Conveyor)...\n");
    if (pwm_setup(PWM_CHANNEL, PWM_PERIOD_NS, PWM_DUTY_NS) != 0) {
        fprintf(stderr, "ERROR: Failed to start PWM\n");
        return;
    }
    
    // 2. Monitor distance until object detected
    printf("Monitoring distance...\n");
    double distance;
    int object_detected = 0;
    
    while (running && !object_detected) {
        distance = sensor_get_distance();
        
        if (distance > 0 && distance < DISTANCE_THRESHOLD) {
            printf("Object detected at %.2f cm!\n", distance);
            object_detected = 1;
        } else if (distance > 0) {
            printf("Distance: %.2f cm\r", distance);
            fflush(stdout);
        }
        
        usleep(100000); // 100ms polling interval
    }
    
    if (!running) return;
    
    // 3. Stop PWM
    printf("\nStopping PWM...\n");
    pwm_disable(PWM_CHANNEL);
    usleep(500000); // Wait 500ms for mechanical settling
    
    // 4. Capture image
    printf("Capturing image...\n");
    if (camera_capture_to_file(IMAGE_PATH) != 0) {
        fprintf(stderr, "ERROR: Image capture failed\n");
        return;
    }
    printf("✓ Image saved to %s\n", IMAGE_PATH);
    
    // 5. Run classifier
    printf("Running classifier...\n");
    int class_id = classifier_predict(IMAGE_PATH);
    
    if (class_id < 0) {
        fprintf(stderr, "ERROR: Classification failed\n");
        return;
    }
    
    printf("✓ Classification result: Class %d\n", class_id);
    
    // 6. Handle defect (Class 1) - Restart PWM and activate servo simultaneously
    if (class_id == 1) {
        printf("\n*** DEFECTIVE ITEM DETECTED ***\n");
        
        // Set defect pin HIGH for 1 second
        set_defect_pin_high();
        sleep(1);
        set_defect_pin_low();
        
        // SIMULTANEOUSLY: Restart PWM and activate servo
        printf("Restarting PWM and activating servo for rejection...\n");
        
        // Restart PWM first
        if (pwm_setup(PWM_CHANNEL, PWM_PERIOD_NS, PWM_DUTY_NS) != 0) {
            fprintf(stderr, "ERROR: Failed to restart PWM\n");
            return;
        }
        printf("✓ PWM restarted (conveyor moving)\n");
        
        // Immediately activate servo (happens while conveyor is running)
        printf("Activating servo for rejection...\n");
        servo_perform_cycle(servo_fd, SERVO_REJECT_ANGLE);
        printf("✓ Servo cycle completed\n");
        
    } else {
        printf("Item passed inspection (Class %d)\n", class_id);
        
        // If item passed, just restart PWM
        printf("\nRestarting PWM (conveyor)...\n");
        if (pwm_setup(PWM_CHANNEL, PWM_PERIOD_NS, PWM_DUTY_NS) != 0) {
            fprintf(stderr, "ERROR: Failed to restart PWM\n");
            return;
        }
        printf("✓ PWM restarted\n");
    }
    
    // 7. Brief delay before next cycle
    printf("\nReady for next item in 1 second...\n");
    sleep(1);
    
    printf("--- Inspection Cycle Complete ---\n\n");
}

// ==========================================
// MAIN FUNCTION
// ==========================================
int main(int argc, char **argv) {
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║  Automated Inspection System v1.0      ║\n");
    printf("║  PolarFire SoC Icicle Kit              ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize all subsystems
    if (initialize_system() != 0) {
        fprintf(stderr, "System initialization failed, exiting.\n");
        return 1;
    }
    
    // Initialize servo (do this once)
    printf("Initializing servo...\n");
    int servo_fd = servo_init();
    if (servo_fd == -1) {
        fprintf(stderr, "ERROR: Servo initialization failed\n");
        cleanup_system();
        return 1;
    }
    printf("✓ Servo initialized\n\n");
    
    // Main control loop
    printf("=== System Active ===\n");
    printf("Waiting for UART command to start...\n");
    printf("Send any character via UART to begin inspection cycle.\n");
    printf("Press Ctrl+C to exit.\n\n");
    
    int system_armed = 0;
    
    while (running) {
        // Check for UART input
        char received = uart_check_input();
        
        if (received != 0) {
            if (!system_armed) {
                printf("\n>>> Received command: '%c' <<<\n", received);
                printf("System ARMED - Starting inspection cycle\n");
                system_armed = 1;
            }
            
            // Run inspection cycle
            run_inspection_cycle(servo_fd);
            
            // After cycle completes, wait for next command
            system_armed = 0;
            printf("\nWaiting for next UART command...\n");
        }
        
        usleep(50000); // 50ms polling for UART
    }
    
    // Cleanup
    servo_close(servo_fd);
    cleanup_system();
    
    printf("\nSystem shutdown complete.\n");
    return 0;
}

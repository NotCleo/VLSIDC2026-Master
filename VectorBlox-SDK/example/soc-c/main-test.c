#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>         // Added for random number generation
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
#define PWM_DUTY_NS 1500000       // 1.5ms pulse width
#define DISTANCE_THRESHOLD 8.0    // cm
#define MODEL_PATH "my_model.vnnx" // Path to your compiled model
#define IMAGE_PATH "capture.jpg"

// GPIO Configuration for Pin 22 (Line 13)
#define GPIO_BASE 512
#define DEFECT_PIN_OFFSET 13      // Line 13 corresponds to Pin 22
#define GPIO_PATH "/sys/class/gpio/"

// Servo angle for defect rejection
#define SERVO_REJECT_ANGLE 25

// ==========================================
// GLOBAL STATE
// ==========================================
static volatile int running = 1;
static int defect_gpio_fd = -1;

// ==========================================
// HMI HELPER FUNCTIONS
// ==========================================
// Helper to send "variable.val=number" to Nextion
void hmi_set_var(const char *var_name, int value) {
    char cmd_buffer[32];
    snprintf(cmd_buffer, sizeof(cmd_buffer), "%s.val=%d", var_name, value);
    uart_hmi_send(cmd_buffer);
}

// ==========================================
// SYSTEM HELPER FUNCTIONS
// ==========================================
// Force kill any process holding /dev/video0
void force_kill_camera(void) {
    // Uses 'fuser' command to find and kill process accessing /dev/video0
    // -k: kill
    // -9: SIGKILL (Force kill)
    // > /dev/null: Silence output (Does not affect functionality)
    system("fuser -k -9 /dev/video0 > /dev/null 2>&1");
    // Increased wait to 200ms to ensure Kernel releases resources completely
    usleep(200000); 
}

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
    
    // 1. Initialize Dual UART (HMI + BT)
    if (uart_init() != 0) {
        fprintf(stderr, "ERROR: UART initialization failed\n");
        return -1;
    }
    printf("✓ UART (HMI & Bluetooth) initialized\n");
    
    // 2. Initialize Ultrasonic Sensor
    if (sensor_init() != 0) {
        fprintf(stderr, "ERROR: Ultrasonic sensor initialization failed\n");
        return -1;
    }
    printf("✓ Ultrasonic sensor initialized\n");
    
    // 3. Ensure Camera is Free (Force Kill any stuck process)
    force_kill_camera();
    printf("✓ Camera resources cleared\n");
    
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
    
    // Set HMI to Offline
    hmi_set_var("blinkMode", 0); 

    // Stop PWM if running
    pwm_disable(PWM_CHANNEL);
    
    // Close UART ports
    uart_close();
    
    // Cleanup sensors
    sensor_cleanup();
    // camera_cleanup(); // Removed here, handled in loop
    
    // Final force kill to ensure no dangling process
    force_kill_camera();
    
    classifier_cleanup();
    
    // Close defect GPIO
    if (defect_gpio_fd != -1) {
        set_defect_pin_low();
        close(defect_gpio_fd);
    }
    
    printf("✓ System cleaned up\n");
}

// ==========================================
// AUTOMATIC INSPECTION LOOP
// ==========================================
// This function loops continuously until 'B' is pressed or signal received
void run_automatic_mode(int servo_fd) {
    printf("\n--- Entering Automatic Inspection Mode ---\n");
    char bt_buffer[64];

    while (running) {
        // --- PHASE 1: SCANNING ---
        
        // Update HMI: Scanning / Idle
        hmi_set_var("state", 0);
        hmi_set_var("pf", 0);
        hmi_set_var("prdID", 0); 
        
        // Start PWM (Conveyor)
        printf("Starting PWM (Conveyor)...\n");
        if (pwm_setup(PWM_CHANNEL, PWM_PERIOD_NS, PWM_DUTY_NS) != 0) {
            fprintf(stderr, "ERROR: Failed to start PWM\n");
            break;
        }
        
        // Monitor distance loop
        printf("Monitoring distance (Waiting for object)...\n");
        double distance;
        int object_detected = 0;
        
        // Keep scanning until object found OR 'B' is pressed
        while (running && !object_detected) {
            // 1. Check UART for Shutdown Command ONLY
            char c = uart_hmi_check_input();
            if (c == 'B' || c == 'b') {
                printf("\n>>> Shutdown command received during scan. Stopping. <<<\n");
                running = 0;
                break;
            }
            // Note: We ignore other characters (noise) here

            // 2. Check Distance
            distance = sensor_get_distance();
            
            if (distance > 0 && distance < DISTANCE_THRESHOLD) {
                printf("Object detected at %.2f cm!\n", distance);
                
                // Update HMI: Object Detected
                hmi_set_var("state", 1);
                
                // Stop motor IMMEDIATELY upon detection
                printf("Stopping motor immediately...\n");
                pwm_disable(PWM_CHANNEL);
                
                object_detected = 1; // Breaks the inner loop
                
            } else if (distance > 0) {
                // Optional: Reduce print frequency to avoid clutter if needed
                printf("Distance: %.2f cm\r", distance);
                fflush(stdout);
            }
            
            usleep(50000); // 50ms polling interval
        }
        
        // If we exited the loop because of shutdown, break the main loop too
        if (!running) break;
        
        // --- PHASE 2: PROCESSING ---
        
        // Mechanical Settling
        usleep(500000); 

        // Update HMI: Processing
        hmi_set_var("state", 2);
        
        // [UPDATED] Ensure previous session is dead BEFORE we try to init
        printf("Ensuring camera device is free...\n");
        force_kill_camera();

        // Initialize Camera (Done PER CYCLE)
        printf("Initializing camera...\n");
        if (camera_init() != 0) {
            fprintf(stderr, "ERROR: Camera initialization failed\n");
            // Break loop if we can't initialize
            break; 
        }

        // Capture image
        printf("Capturing image...\n");
        if (camera_capture_to_file(IMAGE_PATH) != 0) {
            fprintf(stderr, "ERROR: Image capture failed\n");
            // If capture fails, cleanup and retry next loop
            camera_cleanup();
            force_kill_camera();
            continue;
        }
        printf("✓ Image saved to %s\n", IMAGE_PATH);
        
        // Generate 5-digit Random ID & Send via BT
        int unique_id = (rand() % 90000) + 10000;
        snprintf(bt_buffer, sizeof(bt_buffer), "ID:%d\n", unique_id);
        uart_bt_send(bt_buffer);
        printf(">> Bluetooth Sent: %s", bt_buffer);

        // Run classifier
        printf("Running classifier...\n");
        int class_id = classifier_predict(IMAGE_PATH);
        
        if (class_id < 0) {
            fprintf(stderr, "ERROR: Classification failed\n");
            // Cleanup camera and continue
            camera_cleanup();
            force_kill_camera();
            continue;
        } 
        
        // Update HMI Product ID
        hmi_set_var("prdID", class_id);
        
        // Display Logic via Bluetooth/Console
        char result_text[32];
        if (class_id == 1) {
            strcpy(result_text, "DEFECTIVE");
            printf("✓ Result: DEFECTIVE (Class 1)\n");
        } else {
            strcpy(result_text, "NON DEFECTIVE");
            printf("✓ Result: NON DEFECTIVE (Class %d)\n", class_id);
        }

        snprintf(bt_buffer, sizeof(bt_buffer), "RESULT:%s\n", result_text);
        uart_bt_send(bt_buffer);
        printf(">> Bluetooth Sent: %s", bt_buffer);
        
        // Delay 1 second before taking action
        printf("Waiting 1 second before proceeding...\n");
        sleep(1);

        // --- PHASE 3: ACTION & RESTART ---
        
        if (class_id == 1) {
            printf("\n*** DEFECTIVE ITEM ACTION ***\n");

            // Update HMI: Result Damaged
            hmi_set_var("state", 4);
            hmi_set_var("pf", 2);
            
            // GPIO Trigger
            set_defect_pin_high();
            sleep(1);
            set_defect_pin_low();
            
            // Restart PWM
            printf("Restarting PWM and activating servo for rejection...\n");
            if (pwm_setup(PWM_CHANNEL, PWM_PERIOD_NS, PWM_DUTY_NS) != 0) {
                fprintf(stderr, "ERROR: Failed to restart PWM\n");
            }
            
            // Activate Servo
            printf("Activating servo for rejection...\n");
            servo_perform_cycle(servo_fd, SERVO_REJECT_ANGLE);
            printf("✓ Servo cycle completed\n");
            
        } else {
            printf("Item passed inspection.\n");

            // Update HMI: Result Fine
            hmi_set_var("state", 3);
            hmi_set_var("pf", 1);
            
            // Restart PWM
            printf("\nRestarting PWM (conveyor)...\n");
            if (pwm_setup(PWM_CHANNEL, PWM_PERIOD_NS, PWM_DUTY_NS) != 0) {
                fprintf(stderr, "ERROR: Failed to restart PWM\n");
            }
            printf("✓ PWM restarted\n");
        }
        
        // Terminate Camera Process (Done PER CYCLE to release resource)
        printf("Cleaning up camera resource...\n");
        camera_cleanup();
        
        // [UPDATED] Removed redundant force_kill_camera here.
        // It is now done at the start of the next camera phase.

        // Brief delay before next cycle
        printf("\nReady for next item scan in 1 second...\n");
        sleep(1);
        
        // Update HMI: Resetting
        hmi_set_var("state", 5);
        
        printf("--- Item Complete. Looping back to Scan ---\n\n");
    }
}

// ==========================================
// MAIN FUNCTION
// ==========================================
int main(int argc, char **argv) {
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║   Automated Inspection System v2.3     ║\n");
    printf("║   PolarFire SoC Icicle Kit             ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");
    
    // Seed the random number generator
    srand(time(NULL));

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize all subsystems
    if (initialize_system() != 0) {
        fprintf(stderr, "System initialization failed, exiting.\n");
        return 1;
    }
    
    // Initialize servo
    printf("Initializing servo...\n");
    int servo_fd = servo_init();
    if (servo_fd == -1) {
        fprintf(stderr, "ERROR: Servo initialization failed\n");
        cleanup_system();
        return 1;
    }
    printf("✓ Servo initialized\n\n");
    
    // HMI Online
    hmi_set_var("blinkMode", 1);
    hmi_set_var("state", 0);

    // Main Control Logic
    printf("=== System Active ===\n");
    printf("Waiting for initial Start Command (Any Key)...\n");
    
    // Initial blocking wait for start
    while (running) {
        char received = uart_hmi_check_input();
        if (received != 0) {
            if (received == 'B' || received == 'b') {
                printf("Shutdown received immediately. Exiting.\n");
                running = 0;
            } else {
                printf(">>> Start command received: '%c' <<<\n", received);
                // Enter the automatic loop
                run_automatic_mode(servo_fd);
                // When run_automatic_mode returns, running is likely 0 (shutdown)
            }
            break;
        }
        usleep(50000); // 50ms polling
    }
    
    // Cleanup
    servo_close(servo_fd);
    cleanup_system();
    
    printf("\nSystem shutdown complete.\n");
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>   // Added for error reporting
#include <string.h>  // Added for strerror
#include "uart.h"
#include "ultrasonic.h"
#include "camera.h"
#include "classifier.h"

// Note: Conveyor and Servo headers are removed as requested.

void print_menu() {
    printf("\n=== FACTORY SYSTEM DIAGNOSTICS (NO MOTORS) ===\n");
    printf("1. Test UART (Flash Button b1 Green -> White)\n");
    printf("2. Test Ultrasonic (Read distance for 5s)\n");
    printf("3. Test Camera (Take 'test.jpg')\n");
    printf("4. Test Classifier (Run AI on 'test.jpg')\n");
    printf("5. Test Full Sequence (1 Cycle - Simulated Motors)\n");
    printf("0. Exit\n");
    printf("Select Component: ");
}

int main() {
    // --- DEBUG: Print Current Directory ---
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("DEBUG: Current Working Directory is: %s\n", cwd);
        printf("Make sure 'test.jpg' and 'my_model.vnnx' are inside this folder!\n");
    }
    // --------------------------------------

    // 1. Initialize Available Hardware
    printf("Initializing Hardware...\n");
    
    if (uart_init() != 0) printf("UART Init Failed!\n");
    else printf("UART Initialized.\n");

    if (sensor_init() != 0) printf("Sensor Init Failed!\n");
    else printf("Sensor Initialized.\n");
    
    // Camera and AI are initialized on demand or here
    
    int choice;
    while(1) {
        print_menu();
        if (scanf("%d", &choice) != 1) {
            // Clear input buffer if invalid input
            while(getchar() != '\n');
            continue;
        }

        switch(choice) {
            case 0:
                printf("Exiting...\n");
                uart_close();
                return 0;

            case 1: // UART
                printf("Sending HMI Test Commands...\n");
                uart_send_hmi("b1.bco=2016"); 
                printf("Sent Green. Waiting 1 second...\n");
                sleep(1);
                uart_send_hmi("b1.bco=65535"); 
                printf("Sent White. Check your HMI screen.\n");
                break;

            case 2: // SENSOR
                printf("Reading Sensor (Press Ctrl+C to stop early)...\n");
                for(int i=0; i<20; i++) { 
                    double d = sensor_get_distance();
                    if (d < 0) printf("Sensor Error or Out of Range\n");
                    else printf("Distance: %.2f cm\n", d);
                    usleep(250000);
                }
                break;

            case 3: // CAMERA
                printf("Initializing Camera...\n");
                if (camera_init() != 0) {
                    printf("Camera Init Failed!\n");
                    break;
                }
                printf("Capturing test.jpg...\n");
                if (camera_capture_to_file("test.jpg") == 0) {
                    printf("Success! Saved test.jpg\n");
                } else {
                    printf("Capture Failed!\n");
                }
                camera_cleanup();
                break;

            case 4: // CLASSIFIER
                printf("Initializing AI (This loads the model)...\n");
                if (classifier_init("my_model.vnnx") != 0) {
                    printf("AI Init Failed! Check .vnnx file path.\n");
                    break;
                }
                
                // --- DEBUG: File Check ---
                printf("Checking for 'test.jpg'...\n");
                FILE *f = fopen("test.jpg", "rb");
                if (f) {
                    printf("DEBUG: File 'test.jpg' FOUND and OPENED successfully.\n");
                    // Check if it's empty
                    fseek(f, 0, SEEK_END);
                    long fsize = ftell(f);
                    fclose(f);
                    if (fsize == 0) {
                        printf("ERROR: File exists but is EMPTY (0 bytes). Camera failed?\n");
                        break;
                    } else {
                        printf("DEBUG: File size is %ld bytes. Attempting inference...\n", fsize);
                    }
                } else {
                    printf("ERROR: Cannot open 'test.jpg'. System says: %s\n", strerror(errno));
                    break;
                }
                // -------------------------

                printf("Running Inference on 'test.jpg'...\n");
                int id = classifier_predict("test.jpg");
                if (id == -1) {
                    printf("ERROR: Classifier returned -1. This usually means the JPEG format is invalid (e.g. not supported by libjpeg).\n");
                } else {
                    printf(">>> CLASSIFICATION RESULT: Class %d <<<\n", id);
                }
                break;

            case 5: // FULL SEQUENCE SIMULATION
                printf("--- SIMULATING ONE BOX CYCLE ---\n");
                printf("[Simulated] Conveyor Started. Please place object in front of sensor.\n");
                
                int timeout = 100; 
                int object_found = 0;
                
                while(timeout > 0) {
                    double d = sensor_get_distance();
                    if (d > 0 && d < 10.0) {
                        object_found = 1;
                        break;
                    }
                    usleep(100000); 
                    timeout--;
                    if (timeout % 10 == 0) printf("."); 
                    fflush(stdout);
                }
                printf("\n");
                
                if (!object_found) {
                    printf("Timeout! No box seen.\n");
                    break;
                }

                printf("Object Detected at < 10cm! [Simulated] Conveyor Stopped.\n");
                printf("Taking Picture...\n");
                camera_init(); 
                camera_capture_to_file("box.jpg");
                
                printf("Classifying...\n");
                static int ai_ready = 0;
                if (!ai_ready) {
                     if (classifier_init("my_model.vnnx") == 0) ai_ready = 1;
                     else { printf("AI Init Failed\n"); break; }
                }
                
                int cls = classifier_predict("box.jpg");
                printf(">>> RESULT: Class %d <<<\n", cls);
                
                if (cls == 0) {
                    printf("[Simulated] Servo moving LEFT (Apple)\n");
                    uart_send_hmi("t0.txt=\"APPLE\"");
                } else {
                    printf("[Simulated] Servo moving RIGHT (Banana)\n");
                    uart_send_hmi("t0.txt=\"BANANA\"");
                }
                
                printf("Cycle Complete. [Simulated] Conveyor Restarting...\n");
                break;

            default:
                printf("Invalid selection.\n");
        }
        
        printf("\nPress Enter to continue...");
        while(getchar() != '\n'); 
        getchar();
    }
    
    return 0;
}
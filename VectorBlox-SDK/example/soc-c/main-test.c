#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "uart.h"
#include "ultrasonic.h"
#include "camera.h"
#include "classifier.h"

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

int check_jpeg_header(const char* filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;
    
    unsigned char bytes[2];
    if (fread(bytes, 1, 2, f) != 2) {
        fclose(f);
        return 0;
    }
    fclose(f);
    
    // Check for JPEG Magic Bytes (FF D8)
    if (bytes[0] == 0xFF && bytes[1] == 0xD8) return 1;
    
    printf("[ERROR] File %s header is %02X %02X (Expected FF D8)\n", filename, bytes[0], bytes[1]);
    return 0;
}

int main() {
    // --- DEBUG: Print Current Directory ---
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("DEBUG: Current Working Directory is: %s\n", cwd);
    }

    printf("Initializing Hardware...\n");
    
    if (uart_init() != 0) printf("UART Init Failed!\n");
    else printf("UART Initialized.\n");

    if (sensor_init() != 0) printf("Sensor Init Failed!\n");
    else printf("Sensor Initialized.\n");
    
    int choice;
    while(1) {
        print_menu();
        if (scanf("%d", &choice) != 1) {
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
                    system("sync"); // Force write to disk
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
                
                // --- DEBUG: Rigorous File Check ---
                printf("Checking 'test.jpg' integrity...\n");
                system("sync"); // Ensure previous writes are flushed
                
                FILE *f = fopen("test.jpg", "rb");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long fsize = ftell(f);
                    fclose(f);
                    printf("DEBUG: File size is %ld bytes.\n", fsize);
                    
                    if (fsize < 100) { 
                        printf("ERROR: File is too small to be a valid image.\n");
                        break; 
                    }
                    
                    if (!check_jpeg_header("test.jpg")) {
                        printf("ERROR: File is NOT a valid JPEG (Bad Header).\n");
                        break;
                    }
                } else {
                    printf("ERROR: Cannot open 'test.jpg'.\n");
                    break;
                }
                // ----------------------------------

                printf("Running Inference on 'test.jpg'...\n");
                int id = classifier_predict("test.jpg");
                if (id == -1) {
                    printf("ERROR: Classifier returned -1 (Image Format Error). Try taking a new picture with Option 3.\n");
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
                
                printf("Flushing write buffers...\n");
                system("sync"); 
                usleep(200000); 

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
#include "pwm.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

// Path to the PWM controller
//pwmchip0 is the fabric PWMCore
#define PWM_CHIP_PATH "/sys/class/pwm/pwmchip0"

// Initialize the Motor PWM (Export, Set Frequency, Enable)
// period_ns: Frequency (12500ns = 80kHz)
// duty_ns: Initial speed (0 to period_ns)
int pwm_setup(int channel, int period_ns, int duty_ns) {
    char path[256];
    char buffer[50];
    int fd;

    // 1. Export the channel
    // Check if it already exists to avoid "Device or resource busy" errors
    snprintf(path, sizeof(path), "%s/pwm%d/period", PWM_CHIP_PATH, channel);
    if (access(path, F_OK) != 0) {
        // Does not exist, so export it
        snprintf(path, sizeof(path), "%s/export", PWM_CHIP_PATH);
        fd = open(path, O_WRONLY);
        if (fd < 0) { 
            perror("PWM: Failed to open export file"); 
            return -1; 
        }
        snprintf(buffer, sizeof(buffer), "%d", channel);
        if (write(fd, buffer, strlen(buffer)) < 0) {
            perror("PWM: Failed to write to export");
            close(fd);
            return -1;
        }
        close(fd);
        
        // Wait for OS to create the directories
        usleep(100000); 
    }

    // 2. Set Period (Frequency)
    snprintf(path, sizeof(path), "%s/pwm%d/period", PWM_CHIP_PATH, channel);
    fd = open(path, O_WRONLY);
    if (fd < 0) { 
        perror("PWM: Failed to open period"); 
        return -1; 
    }
    snprintf(buffer, sizeof(buffer), "%d", period_ns);
    write(fd, buffer, strlen(buffer));
    close(fd);

    // 3. Set Duty Cycle (Initial Speed)
    snprintf(path, sizeof(path), "%s/pwm%d/duty_cycle", PWM_CHIP_PATH, channel);
    fd = open(path, O_WRONLY);
    if (fd < 0) { 
        perror("PWM: Failed to open duty_cycle"); 
        return -1; 
    }
    snprintf(buffer, sizeof(buffer), "%d", duty_ns);
    write(fd, buffer, strlen(buffer));
    close(fd);

    // 4. Enable the Motor Driver Output
    snprintf(path, sizeof(path), "%s/pwm%d/enable", PWM_CHIP_PATH, channel);
    fd = open(path, O_WRONLY);
    if (fd < 0) { 
        perror("PWM: Failed to open enable"); 
        return -1; 
    }
    write(fd, "1", 1);
    close(fd);

    return 0;
}

// Function to change ONLY the speed (Duty Cycle)
// This is kept incase we needed to change speed without re-initializing
int pwm_set_duty(int channel, int duty_ns) {
    char path[256];
    char buffer[50];
    int fd;

    snprintf(path, sizeof(path), "%s/pwm%d/duty_cycle", PWM_CHIP_PATH, channel);
    fd = open(path, O_WRONLY);
    if (fd < 0) { 
        perror("PWM: Failed to open duty_cycle for update"); 
        return -1; 
    }
    snprintf(buffer, sizeof(buffer), "%d", duty_ns);
    if (write(fd, buffer, strlen(buffer)) < 0) {
        perror("PWM: Failed to write duty_cycle");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

int pwm_disable(int channel) {
    char path[256];
    int fd;

    // Open the enable file
    snprintf(path, sizeof(path), "%s/pwm%d/enable", PWM_CHIP_PATH, channel);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("PWM: Failed to open enable for disabling");
        return -1;
    }

    // Write "0" to disable
    if (write(fd, "0", 1) < 0) {
        perror("PWM: Failed to write disable command");
        close(fd);
        return -1;
    }

    close(fd);
    printf("PWM Channel %d disabled.\n", channel);
    return 0;
}
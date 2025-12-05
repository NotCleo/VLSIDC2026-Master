#include "uart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

//Configuration 
#define HMI_PORT "/dev/ttyS3"
#define BT_PORT  "/dev/ttyS0"
#define BAUD_RATE B9600

// Internal State - Two file descriptors now
static int hmi_fd = -1;
static int bt_fd = -1;

// Define the Nextion/HMI Terminator (3 bytes of 0xFF)
static const unsigned char HMI_TERMINATOR[3] = {0xFF, 0xFF, 0xFF};

// Internal Helper: Configure the Port
// This helper is generic and can configure any file descriptor
static int configure_serial_port(int fd) {
    struct termios tty;

    // Read current attributes
    if (tcgetattr(fd, &tty) != 0) {
        perror("UART: tcgetattr failed");
        return -1;
    }

    // 1. Control Modes (c_cflag)
    tty.c_cflag &= ~PARENB;        // No Parity
    tty.c_cflag &= ~CSTOPB;        // 1 Stop bit
    tty.c_cflag &= ~CSIZE;         // Clear size bits
    tty.c_cflag |= CS8;            // 8 bits per byte
    tty.c_cflag |= CREAD | CLOCAL; // Enable Read, Ignore Modem Status lines

    // 2. Local Modes (c_lflag) - RAW MODE
    tty.c_lflag &= ~ICANON;        // Disable Canonical mode
    tty.c_lflag &= ~ECHO;          // Disable Echo
    tty.c_lflag &= ~ECHOE;         // Disable Erasure
    tty.c_lflag &= ~ISIG;          // Disable Signals

    // 3. Input Modes (c_iflag)
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Disable software flow control
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // 4. Output Modes (c_oflag)
    tty.c_oflag &= ~OPOST;         // Raw output
    tty.c_oflag &= ~ONLCR;         // Don't map Newline to CR-NL

    // 5. Read Blocking Behavior
    // Pure Non-blocking.
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    // 6. Set Baud Rate
    cfsetispeed(&tty, BAUD_RATE);
    cfsetospeed(&tty, BAUD_RATE);

    // Apply attributes
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("UART: tcsetattr failed");
        return -1;
    }

    return 0;
}

// --- Public API ---

int uart_init(void) {
    // 1. Initialize HMI (ttyS0)
    hmi_fd = open(HMI_PORT, O_RDWR | O_NOCTTY | O_SYNC);
    if (hmi_fd < 0) {
        fprintf(stderr, "UART: Failed to open HMI port (%s)\n", HMI_PORT);
        return -1;
    }
    if (configure_serial_port(hmi_fd) != 0) {
        close(hmi_fd);
        hmi_fd = -1;
        return -1;
    }
    printf("UART: HMI initialized on %s\n", HMI_PORT);

    // 2. Initialize Bluetooth (ttyS3)
    bt_fd = open(BT_PORT, O_RDWR | O_NOCTTY | O_SYNC);
    if (bt_fd < 0) {
        fprintf(stderr, "UART: Failed to open Bluetooth port (%s)\n", BT_PORT);
        // Returning -1 ensures strict hardware checking.
        close(hmi_fd); 
        hmi_fd = -1;
        return -1;
    }
    if (configure_serial_port(bt_fd) != 0) {
        close(bt_fd);
        close(hmi_fd);
        bt_fd = -1;
        hmi_fd = -1;
        return -1;
    }
    printf("UART: Bluetooth initialized on %s\n", BT_PORT);

    return 0;
}

// Send specifically to HMI with terminator
void uart_hmi_send(const char *cmd) {
    if (hmi_fd != -1) {
        write(hmi_fd, cmd, strlen(cmd));
        write(hmi_fd, HMI_TERMINATOR, 3);
    }
}

// Check input specifically from HMI
char uart_hmi_check_input(void) {
    if (hmi_fd == -1) return 0;

    unsigned char c;
    int n = read(hmi_fd, &c, 1);
    
    if (n > 0) {
        return (char)c;
    }
    return 0;
}

// Send raw text to Bluetooth
void uart_bt_send(const char *message) {
    if (bt_fd != -1) {
        write(bt_fd, message, strlen(message));
    }
}

void uart_close(void) {
    if (hmi_fd != -1) {
        close(hmi_fd);
        hmi_fd = -1;
        printf("UART: HMI port closed.\n");
    }
    if (bt_fd != -1) {
        close(bt_fd);
        bt_fd = -1;
        printf("UART: Bluetooth port closed.\n");
    }
}

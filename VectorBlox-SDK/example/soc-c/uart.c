#include "uart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

// --- Configuration ---
// On Raspberry Pi 3/4/Zero W, use "/dev/ttyS0" (mini-UART) or "/dev/ttyAMA0"
// Ensure you have enabled serial in raspi-config and disabled the login shell over serial.
#define SERIAL_PORT "/dev/ttyS0"
#define BAUD_RATE B9600

// Internal State
static int serial_fd = -1;

// Define the Nextion/HMI Terminator (3 bytes of 0xFF)
static const unsigned char HMI_TERMINATOR[3] = {0xFF, 0xFF, 0xFF};

// --- Internal Helper: Configure the Port ---
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
    tty.c_lflag &= ~ICANON;        // Disable Canonical mode (read byte-by-byte, not line-by-line)
    tty.c_lflag &= ~ECHO;          // Disable Echo (don't repeat back what we hear)
    tty.c_lflag &= ~ECHOE;         // Disable Erasure
    tty.c_lflag &= ~ISIG;          // Disable Signals (no Ctrl+C handling here)

    // 3. Input Modes (c_iflag)
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Disable software flow control (XON/XOFF)
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL); // Disable special handling

    // 4. Output Modes (c_oflag)
    tty.c_oflag &= ~OPOST;         // Raw output (no processing/formatting)
    tty.c_oflag &= ~ONLCR;         // Don't map Newline to CR-NL

    // 5. Read Blocking Behavior (CRITICAL FOR YOUR LOOP)
    // VMIN = 0, VTIME = 0: Pure Non-blocking.
    // read() returns immediately with 0 if no data is waiting.
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
    // Open Serial Port
    // O_RDWR: Read/Write
    // O_NOCTTY: Do not make this the controlling terminal
    // O_SYNC: Write directly to hardware (don't buffer in OS)
    serial_fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY | O_SYNC);
    
    if (serial_fd < 0) {
        perror("UART: Failed to open device");
        return -1;
    }

    if (configure_serial_port(serial_fd) != 0) {
        close(serial_fd);
        serial_fd = -1;
        return -1;
    }

    // Flush any garbage data sitting in the buffer
    tcflush(serial_fd, TCIOFLUSH);
    
    printf("UART initialized on %s @ 9600 baud\n", SERIAL_PORT);
    return 0;
}

void uart_send_raw(const char *message) {
    if (serial_fd != -1) {
        write(serial_fd, message, strlen(message));
    }
}

void uart_send_hmi(const char *cmd) {
    if (serial_fd != -1) {
        // 1. Send the command string (e.g., "t0.bco=63488")
        write(serial_fd, cmd, strlen(cmd));
        
        // 2. Send the 3 termination bytes immediately after
        write(serial_fd, HMI_TERMINATOR, 3);
    }
}

char uart_check_input(void) {
    if (serial_fd == -1) return 0;

    unsigned char c;
    // Attempt to read 1 byte
    // Because VMIN=0, this will NOT block. 
    // It returns 1 if data exists, 0 if empty, -1 on error.
    int n = read(serial_fd, &c, 1);
    
    if (n > 0) {
        return (char)c; // Return the character found
    }
    
    return 0; // Return 0 if buffer is empty
}

void uart_close(void) {
    if (serial_fd != -1) {
        close(serial_fd);
        serial_fd = -1;
        printf("UART closed.\n");
    }
}
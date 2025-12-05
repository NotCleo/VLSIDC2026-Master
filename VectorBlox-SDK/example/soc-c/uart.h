#ifndef UART_H
#define UART_H

// Initialize both HMI (ttyS3) and Bluetooth (ttyS0)
int uart_init(void);


// HMI FUNCTIONS (ttyS3)

// Send Nextion Command (Automatically adds the 0xFF 0xFF 0xFF)
// Example: uart_hmi_send("t0.txt=\"Hello\"");
void uart_hmi_send(const char *cmd);

// Check for input from HMI (Non-blocking)
// Returns the character read, or 0 if buffer is empty
char uart_hmi_check_input(void);

// BLUETOOTH FUNCTIONS (ttyS0)

// Send raw text to Bluetooth module
// Example: uart_bt_send("Defect Detected\n");
void uart_bt_send(const char *message);

// Close both ports
void uart_close(void);

#endif

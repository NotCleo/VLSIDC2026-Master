#ifndef UART_H
#define UART_H

// Setup
int uart_init(void);

// 1. Send raw text (for debugging or standard serial)
void uart_send_raw(const char *message);

// 2. Send HMI Command (Automatically adds the 0xFF 0xFF 0xFF)
void uart_send_hmi(const char *cmd);

// 3. Check for 'A' or 'B'
char uart_check_input(void);

void uart_close(void);

#endif
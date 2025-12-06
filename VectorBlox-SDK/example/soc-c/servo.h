#ifndef SERVO_H
#define SERVO_H

/**
 * Initializes the GPIO pin for the servo.
 * - Exports the GPIO pin.
 * - Sets direction to 'out'.
 * - Opens the file descriptor for writing values.
 * * Returns: File descriptor (int) to be passed to other functions, or -1 on error.
 */
int servo_init(void);

/**
 * Executes the specific movement pattern:
 * 1. Ensures Servo is at 0 degrees.
 * 2. Moves to 'target_angle' and holds it for 3 seconds.
 * 3. Returns to 0 degrees.
 * * gpio_fd: The file descriptor returned by servo_init().
 * target_angle: The angle to shift to 
 */
void servo_perform_cycle(int gpio_fd, int target_angle);

/**
 * Cleans up and closes the file descriptor.
 */
void servo_close(int gpio_fd);

#endif

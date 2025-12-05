#ifndef PWM_H
#define PWM_H

// Configures and Enables PWM on a specific channel
// channel: The PWM channel on the chip; Here we use channel 0
// period_ns: The total period of the signal in nanoseconds 
// duty_ns: The active duration of the signal in nanoseconds 
// Returns: 0 on success, -1 on error
int pwm_setup(int channel, int period_ns, int duty_ns);

// Disables the PWM output for a specific channel
// Returns: 0 on success, -1 on error
int pwm_disable(int channel);

#endif
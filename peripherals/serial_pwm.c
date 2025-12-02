// This is the integrated UART + PWM application. 
// It reads the inputs given by the user through the Nextion display via UART and then depending upon the button pressed, either starts or stops the converyer belt motors via the PWM channel. 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <ctype.h>
#include <sys/stat.h>

/* --- PWM CONFIGURATION --- */
#define PWM_CHIP_PATH "/sys/class/pwm/pwmchip0"
#define PWM_CHANNEL   0
#define PWM_PERIOD_NS 1000000  // 1 kHz
#define PWM_DUTY_NS   500000   // 50% Duty Cycle

static volatile int keep_running = 1;

/* --- PWM HELPER FUNCTIONS --- */

// Helper to write string values to sysfs files
int pwm_write_file(const char *filename, const char *value) {
    char path[256];
    int fd;

    // Construct full path: /sys/class/pwm/pwmchip0/pwm0/<filename>
    // Exception: 'export' is in the chip root, not the channel folder
    if (strcmp(filename, "export") == 0 || strcmp(filename, "unexport") == 0) {
        snprintf(path, sizeof(path), "%s/%s", PWM_CHIP_PATH, filename);
    } else {
        snprintf(path, sizeof(path), "%s/pwm%d/%s", PWM_CHIP_PATH, PWM_CHANNEL, filename);
    }

    fd = open(path, O_WRONLY);
    if (fd < 0) {
        // It's okay if export fails because it's already exported (EBUSY)
        if (errno == EBUSY && strcmp(filename, "export") == 0) return 0;
        
        fprintf(stderr, "Error opening %s: %s\n", path, strerror(errno));
        return -1;
    }

    if (write(fd, value, strlen(value)) < 0) {
        fprintf(stderr, "Error writing to %s: %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

// Initialize PWM (Export -> Set Period -> Set Duty)
int pwm_init() {
    char buf[32];

    printf("Initializing PWM %d...\n", PWM_CHANNEL);

    // 1. Export the channel
    snprintf(buf, sizeof(buf), "%d", PWM_CHANNEL);
    if (pwm_write_file("export", buf) != 0) {
        // If export failed for a reason other than EBUSY, we might have issues, 
        // but we continue to try setting period.
    }

    // Wait a tiny bit for the filesystem to create the folder if it was just exported
    usleep(100000); 

    // 2. Set Period (Must be done before duty cycle if current duty > new period)
    snprintf(buf, sizeof(buf), "%d", PWM_PERIOD_NS);
    if (pwm_write_file("period", buf) != 0) return -1;

    // 3. Set Duty Cycle
    snprintf(buf, sizeof(buf), "%d", PWM_DUTY_NS);
    if (pwm_write_file("duty_cycle", buf) != 0) return -1;

    // Ensure it starts disabled
    pwm_write_file("enable", "0");

    printf("PWM Initialized (Period: %dns, Duty: %dns)\n", PWM_PERIOD_NS, PWM_DUTY_NS);
    return 0;
}

// Enable (1) or Disable (0) the PWM
void pwm_control(int state) {
    if (state) {
        printf("\n---> [COMMAND] 'A' Received: PWM STARTED\n");
        pwm_write_file("enable", "1");
    } else {
        printf("\n---> [COMMAND] 'B' Received: PWM STOPPED\n");
        pwm_write_file("enable", "0");
    }
}

/* --- SERIAL CONFIGURATION (Original Code) --- */

void int_handler(int signum) {
    (void)signum;
    keep_running = 0;
}

int configure_serial(int fd, int baud) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        return -1;
    }

    tty.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | ISTRIP | IXON | IXOFF | IXANY);
    tty.c_oflag &= ~OPOST;
    tty.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_cc[VMIN] = 1;    
    tty.c_cc[VTIME] = 0;   

    speed_t speed;
    switch (baud) {
        case 9600: speed = B9600; break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 115200: speed = B115200; break;
        default:
            fprintf(stderr, "Unsupported baud %d, using 9600\n", baud);
            speed = B9600;
    }

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        return -1;
    }
    
    tcflush(fd, TCIOFLUSH);
    return 0;
}

/* --- MAIN --- */

int main(int argc, char **argv)
{
    // Fixed missing quote in the original code
    const char *dev = "/dev/ttyS0"; 
    int baud = 9600;

    if (argc >= 2) dev = argv[1];
    if (argc >= 3) baud = atoi(argv[2]);

    // 1. Setup PWM first
    if (pwm_init() != 0) {
        fprintf(stderr, "WARNING: PWM setup failed. Continuing in monitor-only mode.\n");
    }

    // 2. Setup Serial
    printf("Opening serial device: %s at %d baud\n", dev, baud);

    int fd = open(dev, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "ERROR: cannot open %s: %s\n", dev, strerror(errno));
        return 2;
    }

    if (configure_serial(fd, baud) != 0) {
        fprintf(stderr, "ERROR: failed to configure serial port\n");
        close(fd);
        return 3;
    }

    signal(SIGINT, int_handler);
    signal(SIGTERM, int_handler);

    unsigned char buf[256];
    ssize_t n;

    printf("Listening... (Press 'A' to Start PWM, 'B' to Stop PWM)\n");

    while (keep_running) {
        n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read");
            break;
        } else if (n == 0) {
            continue;
        }

        // Process received buffer
        for (ssize_t i = 0; i < n; ++i) {
            unsigned char c = buf[i];
            
            // --- NEW LOGIC START ---
            if (c == 'A' || c == 'a') {
                pwm_control(1); // Start
            } 
            else if (c == 'B' || c == 'b') {
                pwm_control(0); // Stop
            }
            // --- NEW LOGIC END ---

            // Echo to console
            if (isprint(c) || c == '\n' || c == '\r' || c == '\t') {
                putchar(c);
                fflush(stdout);
            } else {
                printf("[0x%02X]", c);
                fflush(stdout);
            }
        }
    }

    // Cleanup: Turn off PWM on exit? (Optional, currently leaves it as is)
    // pwm_control(0); 
    
    printf("\nExiting %s\n", dev);
    close(fd);
    return 0;
}

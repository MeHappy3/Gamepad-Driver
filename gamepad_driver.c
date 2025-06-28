// max186_gamepad_configurable.c

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/spi/spidev.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#define SPI_PATH "/dev/spidev0.0"
#define MAX_SPEED 2000000
#define MAX_BUTTONS 32
#define MAX_LINE 128
#define CONFIG_PATH "gamepad.conf"
#define ADC_MAX 2048
#define UINPUT_AXIS_MAX 255

// Supported buttons with names
struct ButtonConfig {
    char name[32];
    int gpio;
    int keycode;
};

struct ButtonConfig buttons[MAX_BUTTONS];
int button_count = 0;

int axis_channels[4] = {0, 4, 1, 5}; // LEFTX, LEFTY, RIGHTX, RIGHTY

// Map name to uinput keycode
int map_button_name(const char *name) {
    if (strcmp(name, "A") == 0) return BTN_A;
    if (strcmp(name, "B") == 0) return BTN_B;
    if (strcmp(name, "X") == 0) return BTN_X;
    if (strcmp(name, "Y") == 0) return BTN_Y;
    if (strcmp(name, "DPAD_UP") == 0) return BTN_DPAD_UP;
    if (strcmp(name, "DPAD_DOWN") == 0) return BTN_DPAD_DOWN;
    if (strcmp(name, "DPAD_LEFT") == 0) return BTN_DPAD_LEFT;
    if (strcmp(name, "DPAD_RIGHT") == 0) return BTN_DPAD_RIGHT;
    if (strcmp(name, "LEFT_TRIG1") == 0) return BTN_TL;
    if (strcmp(name, "LEFT_TRIG2") == 0) return BTN_TL2;
    if (strcmp(name, "RIGHT_TRIG1") == 0) return BTN_TR;
    if (strcmp(name, "RIGHT_TRIG2") == 0) return BTN_TR2;
    if (strcmp(name, "THUMBL") == 0) return BTN_THUMBL;
    if (strcmp(name, "THUMBR") == 0) return BTN_THUMBR;
    if (strcmp(name, "STRT") == 0) return BTN_START;
    if (strcmp(name, "SEL") == 0) return BTN_SELECT;
    return -1;
}

// Simple config parser
void load_config(const char *path) {
    FILE *ff = fopen(path, "r");
    if (!ff) {
        perror("fopen");
        return;
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), ff)) {
        // Remove newline
        line[strcspn(line, "\r\n")] = 0;

        if (line[0] == '#' || line[0] == '[' || strlen(line) < 3)
            continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = 0;
        char *name = line;
        char *value = eq + 1;

        int keycode = map_button_name(name);
        if (keycode < 0) continue;

        int gpio = atoi(value);
        strncpy(buttons[button_count].name, name, 31);
        buttons[button_count].gpio = gpio;
        buttons[button_count].keycode = keycode;

        printf("Button %d (%s) = GPIO %d (keycode %d)\n", button_count, name, gpio, keycode);
        button_count++;
    }

    fclose(ff);
}

int export_gpio(int gpio) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    if (access(path, F_OK) == 0) return 0;
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) return -1;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", gpio);
    write(fd, buf, strlen(buf));
    close(fd);
    usleep(100000);
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, "in", 2);
    close(fd);
    return 0;




}

int read_gpio(int gpio) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    char val = '0';
    read(fd, &val, 1);
    close(fd);
    return val == '1';
}

int read_adc(int fd, uint8_t channel) {
    uint8_t tx[] = { 0x8F | (channel << 4), 0x00, 0x00 };
    uint8_t rx[3] = {0};
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = 3,
        .speed_hz = MAX_SPEED,
        .bits_per_word = 8
    };
    ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    return (rx[1] << 4) | (rx[2] >> 4);
}

int scale_axis(int val, int invert) {
    int scaled = (val * UINPUT_AXIS_MAX) / ADC_MAX;
    return invert ? (UINPUT_AXIS_MAX - scaled) : scaled;
}

int main() {
    load_config(CONFIG_PATH);

    int spi_fd = open(SPI_PATH, O_RDWR);
    if (spi_fd < 0) { perror("SPI open"); return 1; }
    uint32_t speed = MAX_SPEED;
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    for (int i = 0; i < button_count; ++i)
        export_gpio(buttons[i].gpio);

    int ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    ioctl(ufd, UI_SET_EVBIT, EV_KEY);
    ioctl(ufd, UI_SET_EVBIT, EV_ABS);

    for (int i = 0; i < button_count; ++i)
        ioctl(ufd, UI_SET_KEYBIT, buttons[i].keycode);

    ioctl(ufd, UI_SET_ABSBIT, ABS_X);
    ioctl(ufd, UI_SET_ABSBIT, ABS_Y);
    ioctl(ufd, UI_SET_ABSBIT, ABS_RX);
    ioctl(ufd, UI_SET_ABSBIT, ABS_RY);

    struct uinput_user_dev uidev = {0};
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "MAX186 Gamepad");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x6969;
    uidev.id.product = 0x9696;

    uidev.absmax[ABS_X] = uidev.absmax[ABS_Y] = UINPUT_AXIS_MAX;
    uidev.absmax[ABS_RX] = uidev.absmax[ABS_RY] = UINPUT_AXIS_MAX;

    write(ufd, &uidev, sizeof(uidev));
    ioctl(ufd, UI_DEV_CREATE);
    usleep(100000);

    while (1) {
        struct input_event ev;
        gettimeofday(&ev.time, NULL);

        int left_x = scale_axis(read_adc(spi_fd, axis_channels[0]), 0);
        int left_y = scale_axis(read_adc(spi_fd, axis_channels[1]), 0);
        int right_x = scale_axis(read_adc(spi_fd, axis_channels[2]), 1);
        int right_y = scale_axis(read_adc(spi_fd, axis_channels[3]), 1);

        ev.type = EV_ABS; 
        ev.code = ABS_X; ev.value = left_x; write(ufd, &ev, sizeof(ev));
        ev.code = ABS_Y; ev.value = left_y; write(ufd, &ev, sizeof(ev));
        ev.code = ABS_RX; ev.value = right_x; write(ufd, &ev, sizeof(ev));
        ev.code = ABS_RY; ev.value = right_y; write(ufd, &ev, sizeof(ev));

        for (int i = 0; i < button_count; ++i) {
            ev.type = EV_KEY;
            ev.code = buttons[i].keycode;
            ev.value = read_gpio(buttons[i].gpio);
            write(ufd, &ev, sizeof(ev));
        }

        ev.type = EV_SYN; ev.code = SYN_REPORT; ev.value = 0;
        write(ufd, &ev, sizeof(ev));
        usleep(1000);
    }

    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    close(spi_fd);
    return 0;
}


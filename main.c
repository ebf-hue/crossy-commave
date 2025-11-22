#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define GPIO_UP 67
#define GPIO_DOWN 68
#define GPIO_LEFT 26
#define GPIO_RIGHT 44
#define GPIO_PATH "/sys/class/gpio"
#define MOVE_STEP 10

// Global variables
static int fb_fd = -1;
static unsigned short *fbp = NULL;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static unsigned long screensize;
static unsigned char *image_data = NULL;
static int img_width, img_height;
static int image_x_pos = 0;
static int image_y_pos = 0;
static volatile int running = 1;

// Convert RGB to RGB565
uint16_t rgb_to_rgb565(unsigned char r, unsigned char g, unsigned char b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// GPIO helper functions
int gpio_export(int gpio) {
    char path[64];
    char buf[4];
    
    // Check if already exported
    snprintf(path, sizeof(path), GPIO_PATH "/gpio%d", gpio);
    if (access(path, F_OK) == 0) {
        printf("GPIO %d already exported\n", gpio);
        return 0;
    }
    
    int fd = open(GPIO_PATH "/export", O_WRONLY);
    if (fd < 0) {
        perror("Failed to open export");
        return -1;
    }
    snprintf(buf, sizeof(buf), "%d", gpio);
    write(fd, buf, strlen(buf));
    close(fd);
    usleep(100000); // Wait for GPIO to be exported
    return 0;
}

int gpio_unexport(int gpio) {
    int fd = open(GPIO_PATH "/unexport", O_WRONLY);
    if (fd < 0) return -1;
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", gpio);
    write(fd, buf, strlen(buf));
    close(fd);
    return 0;
}

int gpio_set_direction(int gpio, const char *direction) {
    char path[64];
    snprintf(path, sizeof(path), GPIO_PATH "/gpio%d/direction", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("Failed to set direction");
        return -1;
    }
    write(fd, direction, strlen(direction));
    close(fd);
    return 0;
}

int gpio_get_value(int gpio) {
    char path[64];
    char value;
    snprintf(path, sizeof(path), GPIO_PATH "/gpio%d/value", gpio);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    read(fd, &value, 1);
    close(fd);
    return (value == '1') ? 1 : 0;
}

// Disable cursor
void disable_cursor() {
    // Disable text cursor via escape sequence
    const char cursor_off[] = "\033[?25l";
    write(STDOUT_FILENO, cursor_off, sizeof(cursor_off) - 1);
    
    // Try to disable framebuffer cursor
    int ret = ioctl(fb_fd, FBIO_CURSOR, 0);
    if (ret == 0) {
        printf("Cursor disabled\n");
    }
}

// Draw image at specified X and Y position
void draw_image(int x_offset, int y_offset) {
    int x, y;
    
    // Clear screen (black)
    memset(fbp, 0, screensize);
    
    // Use provided offsets
    int start_x = x_offset;
    int start_y = y_offset;
    
    // Draw image
    for (y = 0; y < img_height; y++) {
        int screen_y = start_y + y;
        if (screen_y < 0 || screen_y >= vinfo.yres) continue;
        
        for (x = 0; x < img_width; x++) {
            int screen_x = start_x + x;
            if (screen_x < 0 || screen_x >= vinfo.xres) continue;
            
            // Get RGB values from image
            int img_idx = (y * img_width + x) * 3;
            unsigned char r = image_data[img_idx];
            unsigned char g = image_data[img_idx + 1];
            unsigned char b = image_data[img_idx + 2];
            
            // Calculate framebuffer position using line_length
            unsigned long fb_offset = screen_y * finfo.line_length + screen_x * 2;
            unsigned short *pixel = (unsigned short *)((char *)fbp + fb_offset);
            
            // Write RGB565 pixel
            *pixel = rgb_to_rgb565(r, g, b);
        }
    }
}

void cleanup() {
    running = 0;
    
    // Re-enable cursor
    const char cursor_on[] = "\033[?25h";
    write(STDOUT_FILENO, cursor_on, sizeof(cursor_on) - 1);
    
    // Unmap framebuffer
    if (fbp != NULL && fbp != MAP_FAILED) {
        munmap(fbp, screensize);
    }
    
    // Close framebuffer
    if (fb_fd >= 0) {
        close(fb_fd);
    }
    
    // Free image data
    if (image_data) {
        stbi_image_free(image_data);
    }
    
    // Unexport GPIOs
    gpio_unexport(GPIO_UP);
    gpio_unexport(GPIO_DOWN);
    gpio_unexport(GPIO_LEFT);
    gpio_unexport(GPIO_RIGHT);
    
    printf("Cleanup complete\n");
}

void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        printf("\nReceived signal, exiting...\n");
        cleanup();
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    int up_prev = 0, down_prev = 0, left_prev = 0, right_prev = 0;
    int up_curr, down_curr, left_curr, right_curr;
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Loading image...\n");
    
    // Load image using stb_image (force 3 channels RGB)
    image_data = stbi_load("unnamed.png", &img_width, &img_height, NULL, 3);
    if (!image_data) {
        fprintf(stderr, "Error: Could not load image 'unnamed.png'\n");
        return 1;
    }
    
    printf("Image loaded: %dx%d\n", img_width, img_height);
    
    // Open framebuffer
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        fprintf(stderr, "Error: Cannot open framebuffer\n");
        stbi_image_free(image_data);
        return 1;
    }
    
    // Get screen info
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);
    
    printf("Screen: %dx%d, %d bpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
    printf("Line length: %d bytes\n", finfo.line_length);
    
    // Calculate screen size using line_length
    screensize = finfo.line_length * vinfo.yres;
    
    // Map framebuffer
    fbp = (unsigned short *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fbp == MAP_FAILED) {
        fprintf(stderr, "Error: Failed to map framebuffer\n");
        close(fb_fd);
        stbi_image_free(image_data);
        return 1;
    }
    
    printf("Framebuffer mapped\n");
    
    // Disable cursor
    disable_cursor();
    
    // Set up GPIO buttons
    printf("Setting up GPIO buttons...\n");
    if (gpio_export(GPIO_UP) < 0) {
        fprintf(stderr, "Warning: Could not export UP\n");
    }
    if (gpio_export(GPIO_DOWN) < 0) {
        fprintf(stderr, "Warning: Could not export DOWN\n");
    }
    if (gpio_export(GPIO_LEFT) < 0) {
        fprintf(stderr, "Warning: Could not export LEFT\n");
    }
    if (gpio_export(GPIO_RIGHT) < 0) {
        fprintf(stderr, "Warning: Could not export RIGHT\n");
    }
    gpio_set_direction(GPIO_UP, "in");
    gpio_set_direction(GPIO_DOWN, "in");
    gpio_set_direction(GPIO_LEFT, "in");
    gpio_set_direction(GPIO_RIGHT, "in");
    
    // Initialize image position (centered)
    image_x_pos = (vinfo.xres - img_width) / 2;
    image_y_pos = (vinfo.yres - img_height) / 2;
    if (image_x_pos < 0) image_x_pos = 0;
    if (image_y_pos < 0) image_y_pos = 0;
    
    // Draw initial image
    draw_image(image_x_pos, image_y_pos);
    
    printf("Ready! UP=67, DOWN=68, LEFT=26, RIGHT=44. Press Ctrl+C to exit.\n");
    
    // Main loop
    while (running) {
        // Read current button states
        up_curr = gpio_get_value(GPIO_UP);
        down_curr = gpio_get_value(GPIO_DOWN);
        left_curr = gpio_get_value(GPIO_LEFT);
        right_curr = gpio_get_value(GPIO_RIGHT);
        
        // Detect button presses (rising edge)
        if (up_curr && !up_prev) {
            // UP pressed - move image UP
            image_y_pos -= MOVE_STEP;
            if (image_y_pos < 0) image_y_pos = 0;
            draw_image(image_x_pos, image_y_pos);
            printf("UP pressed - Moving UP to (%d, %d)\n", image_x_pos, image_y_pos);
        }
        
        if (down_curr && !down_prev) {
            // DOWN pressed - move image DOWN
            image_y_pos += MOVE_STEP;
            int max_y = vinfo.yres - img_height;
            if (image_y_pos > max_y) image_y_pos = max_y;
            if (image_y_pos < 0) image_y_pos = 0;
            draw_image(image_x_pos, image_y_pos);
            printf("DOWN pressed - Moving DOWN to (%d, %d)\n", image_x_pos, image_y_pos);
        }
        
        if (left_curr && !left_prev) {
            // LEFT pressed - move image LEFT
            image_x_pos -= MOVE_STEP;
            if (image_x_pos < 0) image_x_pos = 0;
            draw_image(image_x_pos, image_y_pos);
            printf("LEFT pressed - Moving LEFT to (%d, %d)\n", image_x_pos, image_y_pos);
        }
        
        if (right_curr && !right_prev) {
            // RIGHT pressed - move image RIGHT
            image_x_pos += MOVE_STEP;
            int max_x = vinfo.xres - img_width;
            if (image_x_pos > max_x) image_x_pos = max_x;
            if (image_x_pos < 0) image_x_pos = 0;
            draw_image(image_x_pos, image_y_pos);
            printf("RIGHT pressed - Moving RIGHT to (%d, %d)\n", image_x_pos, image_y_pos);
        }
        
        // Update previous button states
        up_prev = up_curr;
        down_prev = down_curr;
        left_prev = left_curr;
        right_prev = right_curr;
        
        // Small delay to prevent CPU hogging
        usleep(50000); // 50ms
    }
    
    cleanup();
    return 0;
}

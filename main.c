// SDL2 on laptop, framebuffer+GPIO on BeagleBone

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ---------- Common config ----------

#define MOVE_STEP 38

static unsigned char *image_data = NULL;
static int img_width = 0, img_height = 0;
static int image_x_pos = 0;
static int image_y_pos = 0;
static int screen_width = 480;
static int screen_height = 272;
static volatile int running = 1;

// Platform abstraction
int  platform_init(void);
void platform_shutdown(void);
void clear_screen(void);
void put_pixel(int x, int y, uint16_t color);
void present_frame(void);
void poll_input(int *up, int *down, int *left, int *right, int *quit);

// ---------- Shared helpers ----------

static uint16_t rgb_to_rgb565(unsigned char r, unsigned char g, unsigned char b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void draw_image(int x_offset, int y_offset) {
    clear_screen();

    for (int y = 0; y < img_height; y++) {
        int screen_y = y_offset + y;
        if (screen_y < 0 || screen_y >= screen_height) continue;

        for (int x = 0; x < img_width; x++) {
            int screen_x = x_offset + x;
            if (screen_x < 0 || screen_x >= screen_width) continue;

            int img_idx = (y * img_width + x) * 3;
            unsigned char r = image_data[img_idx];
            unsigned char g = image_data[img_idx + 1];
            unsigned char b = image_data[img_idx + 2];

            uint16_t color = rgb_to_rgb565(r, g, b);
            put_pixel(screen_x, screen_y, color);
        }
    }
}

// ---------- SDL backend (laptop / macOS) ----------

#ifdef USE_SDL

#include <SDL2/SDL.h>

static SDL_Window   *window   = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture  *texture  = NULL;
static uint16_t     *framebuffer = NULL;

int platform_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    window = SDL_CreateWindow(
        "Sprite Test",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        screen_width, screen_height,
        SDL_WINDOW_SHOWN
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return -1;
    }

    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB565,
        SDL_TEXTUREACCESS_STREAMING,
        screen_width,
        screen_height
    );
    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return -1;
    }

    framebuffer = (uint16_t *)malloc(screen_width * screen_height * sizeof(uint16_t));
    if (!framebuffer) {
        fprintf(stderr, "Failed to allocate framebuffer\n");
        return -1;
    }

    return 0;
}

void platform_shutdown(void) {
    if (framebuffer) {
        free(framebuffer);
        framebuffer = NULL;
    }
    if (texture) {
        SDL_DestroyTexture(texture);
        texture = NULL;
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    SDL_Quit();
}

void clear_screen(void) {
    memset(framebuffer, 0, screen_width * screen_height * sizeof(uint16_t));
}

void put_pixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= screen_width || y < 0 || y >= screen_height) return;
    framebuffer[y * screen_width + x] = color;
}

void present_frame(void) {
    SDL_UpdateTexture(texture, NULL, framebuffer, screen_width * sizeof(uint16_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

void poll_input(int *up, int *down, int *left, int *right, int *quit) {
    *up = *down = *left = *right = 0;
    SDL_Event e;

    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            *quit = 1;
        } else if (e.type == SDL_KEYDOWN) {
            switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: *quit = 1; break;
                case SDLK_UP:     *up    = 1; break;
                case SDLK_DOWN:   *down  = 1; break;
                case SDLK_LEFT:   *left  = 1; break;
                case SDLK_RIGHT:  *right = 1; break;
                default: break;
            }
        }
    }
}

#endif  // USE_SDL

// ---------- Framebuffer + GPIO backend (BeagleBone) ----------

#ifndef USE_SDL

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <signal.h>
#include <errno.h>

#define GPIO_BTN0 26
#define GPIO_BTN1 46
#define GPIO_BTN2 47
#define GPIO_BTN3 27
#define GPIO_PATH "/sys/class/gpio"

static int fb_fd = -1;
static unsigned short *fbp = NULL;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static unsigned long screensize = 0;

static int gpio_export(int gpio) {
    char path[64];
    char buf[16];

    snprintf(path, sizeof(path), GPIO_PATH "/gpio%d", gpio);
    if (access(path, F_OK) == 0) {
        return 0; // already exported
    }

    int fd = open(GPIO_PATH "/export", O_WRONLY);
    if (fd < 0) {
        perror("gpio_export: open export");
        return -1;
    }
    snprintf(buf, sizeof(buf), "%d", gpio);
    if (write(fd, buf, strlen(buf)) < 0) {
        perror("gpio_export: write");
        close(fd);
        return -1;
    }
    close(fd);
    usleep(100000);
    return 0;
}

static int gpio_unexport(int gpio) {
    int fd = open(GPIO_PATH "/unexport", O_WRONLY);
    if (fd < 0) return -1;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", gpio);
    write(fd, buf, strlen(buf));
    close(fd);
    return 0;
}

static int gpio_set_direction(int gpio, const char *direction) {
    char path[64];
    snprintf(path, sizeof(path), GPIO_PATH "/gpio%d/direction", gpio);
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("gpio_set_direction");
        return -1;
    }
    write(fd, direction, strlen(direction));
    close(fd);
    return 0;
}

static int gpio_get_value(int gpio) {
    char path[64];
    char value;
    snprintf(path, sizeof(path), GPIO_PATH "/gpio%d/value", gpio);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    if (read(fd, &value, 1) < 1) {
        close(fd);
        return 0;
    }
    close(fd);
    return (value == '1') ? 1 : 0;
}

static void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        running = 0;
    }
}

int platform_init(void) {
    // load framebuffer
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        perror("open /dev/fb0");
        return -1;
    }

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("FBIOGET_VSCREENINFO");
        return -1;
    }
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("FBIOGET_FSCREENINFO");
        return -1;
    }

    screensize = finfo.line_length * vinfo.yres;
    fbp = (unsigned short *)mmap(0, screensize, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fb_fd, 0);
    if (fbp == MAP_FAILED) {
        perror("mmap framebuffer");
        fbp = NULL;
        return -1;
    }

    screen_width  = vinfo.xres;
    screen_height = vinfo.yres;

    // GPIO setup
    if (gpio_export(GPIO_BTN0) < 0 ||
        gpio_export(GPIO_BTN1) < 0 ||
        gpio_export(GPIO_BTN2) < 0 ||
        gpio_export(GPIO_BTN3) < 0) {
        fprintf(stderr, "Warning: some GPIOs could not be exported\n");
    }

    gpio_set_direction(GPIO_BTN0, "in");
    gpio_set_direction(GPIO_BTN1, "in");
    gpio_set_direction(GPIO_BTN2, "in");
    gpio_set_direction(GPIO_BTN3, "in");

    // signals
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    return 0;
}

void platform_shutdown(void) {
    // Unmap framebuffer
    if (fbp && fbp != MAP_FAILED) {
        munmap(fbp, screensize);
        fbp = NULL;
    }
    if (fb_fd >= 0) {
        close(fb_fd);
        fb_fd = -1;
    }

    // Unexport GPIOs
    gpio_unexport(GPIO_BTN0);
    gpio_unexport(GPIO_BTN1);
    gpio_unexport(GPIO_BTN2);
    gpio_unexport(GPIO_BTN3);
}

void clear_screen(void) {
    if (fbp && screensize > 0) {
        memset(fbp, 0, screensize);
    }
}

void put_pixel(int x, int y, uint16_t color) {
    if (!fbp) return;
    if (x < 0 || x >= vinfo.xres || y < 0 || y >= vinfo.yres) return;

    unsigned long fb_offset = y * finfo.line_length + x * 2;
    unsigned short *pixel = (unsigned short *)((char *)fbp + fb_offset);
    *pixel = color;
}

void present_frame(void) {
    // nothing needed for real framebuffer
}

void poll_input(int *up, int *down, int *left, int *right, int *quit) {
    *quit = 0;
    *up    = gpio_get_value(GPIO_BTN0);
    *down  = gpio_get_value(GPIO_BTN1);
    *left  = gpio_get_value(GPIO_BTN2);
    *right = gpio_get_value(GPIO_BTN3);
}

#endif  // !USE_SDL

// ---------- Shared main ----------

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("Loading image...\n");
    image_data = stbi_load("assets/guy1.png", &img_width, &img_height, NULL, 3);
    if (!image_data) {
        fprintf(stderr, "Error: Could not load image 'guy1.png'\n");
        return 1;
    }
    printf("Image loaded: %dx%d\n", img_width, img_height);

    if (platform_init() != 0) {
        stbi_image_free(image_data);
        return 1;
    }

    // initial sprite position (centered as much as possible)
    image_x_pos = (screen_width  - img_width)  / 2;
    image_y_pos = (screen_height - img_height) / 2;
    if (image_x_pos < 0) image_x_pos = 0;
    if (image_y_pos < 0) image_y_pos = 0;

    printf("Controls:\n");
#ifdef USE_SDL
    printf("  Arrow keys move sprite, ESC or window close to exit.\n");
#else
    printf("  BTN0=Up, BTN1=Down, BTN2=Left, BTN3=Right, Ctrl+C to exit.\n");
#endif

    while (running) {
        int up, down, left, right, quit = 0;

        poll_input(&up, &down, &left, &right, &quit);
        if (quit) {
            running = 0;
        }

        if (up)    image_y_pos -= MOVE_STEP;
        if (down)  image_y_pos += MOVE_STEP;
        if (left)  image_x_pos -= MOVE_STEP;
        if (right) image_x_pos += MOVE_STEP;

        // clamp inside screen
        if (image_x_pos < 0) image_x_pos = 0;
        if (image_y_pos < 0) image_y_pos = 0;
        if (image_x_pos > screen_width - img_width)
            image_x_pos = screen_width - img_width;
        if (image_y_pos > screen_height - img_height)
            image_y_pos = screen_height - img_height;

        draw_image(image_x_pos, image_y_pos);
        present_frame();

#ifdef USE_SDL
        SDL_Delay(16);      // ~60 FPS
#else
        usleep(50000);      // 50 ms
#endif
    }

    platform_shutdown();

    if (image_data) {
        stbi_image_free(image_data);
        image_data = NULL;
    }

    return 0;
}


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "declarations.h"

// SDL backend (laptop / macOS)

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

// Framebuffer + GPIO backend (BeagleBone)-----------------------------------------

#ifndef USE_SDL

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <signal.h>
#include <errno.h>

static int fb_fd = -1;
static unsigned short *fbp = NULL;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static unsigned long screensize = 0;
static int up_curr = 0, down_curr = 0, left_curr = 0, right_curr = 0;
static int up_prev = 0, down_prev = 0, left_prev = 0, right_prev = 0;
static unsigned short *backbuffer = NULL;

//assign pin:
int gpio_export(int gpio) {
    char path[64];
    char buf[4];

    int fd = open(GPIO_PATH "/export", O_WRONLY);
    if (fd < 0) {
        perror("Failed to open export");
        return -1;
    }
    snprintf(buf, sizeof(buf), "%d", gpio);
    write(fd, buf, strlen(buf));
    close(fd);

    usleep(100000);  //HALT!
    return 0; 
}

//unassign pin:
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

// for CTRL+C 
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

    //get screen info
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        perror("FBIOGET_VSCREENINFO");
        close(fb_fd);
        return -1;
    }
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("FBIOGET_FSCREENINFO");
        close(fb_fd);
        return -1;
    }
    
    screensize = finfo.line_length * vinfo.yres;

    fbp = (unsigned short *)mmap(0, screensize, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fb_fd, 0);
    if (fbp == MAP_FAILED) {
        perror("mmap framebuffer");
        fbp = NULL;
        close(fb_fd);
        return -1;
    }

    //double buffer :(
    backbuffer = (unsigned short *)malloc(screensize);
    if (!backbuffer) {
        perror("malloc backbuffer failed");
        munmap(fbp, screensize);
        fbp = NULL;
        close(fb_fd);
        return -1;
    }

    screen_width  = vinfo.xres;
    screen_height = vinfo.yres;

    //now setup gpios
    if (gpio_export(GPIO_BTN0) < 0) {
        fprintf(stderr, "Warning: Could not export UP\n");
    }
    if (gpio_export(GPIO_BTN1) < 0) {
        fprintf(stderr, "Warning: Could not export DOWN\n");
    }
    if (gpio_export(GPIO_BTN2) < 0) {
        fprintf(stderr, "Warning: Could not export LEFT\n");
    }
    if (gpio_export(GPIO_BTN3) < 0) {
        fprintf(stderr, "Warning: Could not export RIGHT\n");
    }

    //outputs
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

    //bye bye buffer
    if (backbuffer) {
        free(backbuffer);
        backbuffer = NULL;
    }
    if (fbp && fbp != MAP_FAILED) {
        munmap(fbp, screensize);
        fbp = NULL;
    }
    if (fb_fd >= 0) {
        close(fb_fd);
        fb_fd = -1;
    }

    //bye bye gpios
    gpio_unexport(GPIO_BTN0);
    gpio_unexport(GPIO_BTN1);
    gpio_unexport(GPIO_BTN2);
    gpio_unexport(GPIO_BTN3);
}

void clear_screen(void) {
    if (backbuffer && screensize > 0) {
        memset(backbuffer, 0, screensize); 
    }
}

void put_pixel(int x, int y, uint16_t color) {
    if (!backbuffer) return;
    if (x < 0 || x >= vinfo.xres || y < 0 || y>= vinfo.yres) return;

    unsigned long fb_offset = y * finfo.line_length + x * 2;
    unsigned short *pixel = (unsigned short *)((char *)backbuffer + fb_offset);
    *pixel = color;
}

void present_frame(void) {
    if (fbp && backbuffer && screensize > 0) {
        memcpy(fbp, backbuffer, screensize);
    }
}

void poll_input(int *up, int *down, int *left, int *right, int *quit) {

    *up = *down = *left = *right = *quit = 0;
    
    //current values
    up_curr = gpio_get_value(GPIO_BTN0);
    down_curr = gpio_get_value(GPIO_BTN1);
    left_curr = gpio_get_value(GPIO_BTN2);
    right_curr = gpio_get_value(GPIO_BTN3);

    // if a value less than 0, error occured, return
    if (up_curr < 0 || down_curr < 0 || left_curr < 0 || right_curr < 0) {
        return;
    }
    
    //LATCHING: if previous state not 1, but current state 1, button pressed
    if (up_curr && !up_prev) {
        *up = 1;
        }
    if (down_curr && !down_prev) {
        *down = 1;
    }
    if (left_curr && !left_prev) {
        *left = 1;
    }
    if (right_curr && !right_prev) {
        *right = 1;
    }

    //update states
    up_prev = up_curr;
    down_prev = down_curr;
    left_prev = left_curr;
    right_prev = right_curr;

}

#endif

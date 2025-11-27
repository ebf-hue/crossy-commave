// SDL2 on laptop, framebuffer+GPIO on BeagleBone

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ---------- Common config ----------

#define MOVE_STEP 34

static unsigned char *image_data = NULL;
static int img_width = 0, img_height = 0;
static int image_x_pos = 0;
static int image_y_pos = 0;
static int screen_width = 480;
static int screen_height = 272;
static volatile int running = 1;

// Lane management
#define LANE_HEIGHT 34
#define MAX_VISIBLE_LANES 10  // screen_height/LANE_HEIGHT + buffer
#define NUM_MBTA_LANES 3  // Number of MBTA lanes to randomly place
#define NUM_LEVELS 5

// Level definitions
typedef struct {
    int total_lanes;
    int num_mbta_pairs;
} LevelConfig;

static LevelConfig levels[NUM_LEVELS] = {
    {12, 1},   // Level 1: 10 game lanes + 2 start lanes, 1 MBTA pair
    {17, 2},   // Level 2: 15 game lanes + 2 start lanes, 2 MBTA pairs
    {22, 3},   // Level 3: 20 game lanes + 2 start lanes, 3 MBTA pairs
    {27, 4},   // Level 4: 25 game lanes + 2 start lanes, 4 MBTA pairs
    {32, 5}    // Level 5: 30 game lanes + 2 start lanes, 5 MBTA pairs
};

typedef struct {
    unsigned char *data;
    int width;
    int height;
} Lane;

static Lane lane_templates[6];  // 0=bottom, 1=middle, 2=top, 3=MBTA, 4=start, 5=building
static int num_lane_types = 0;
static int camera_y = 0;  // Camera offset in world space
static int first_lane_index = 0;  // Which lane is at the top
static int mbta_lane_indices[35];  // Support up to 35 lanes max
static int current_level = 0;
static int total_lanes_current = 0;

// Level passed popup
static unsigned char *level_passed_data = NULL;
static int level_passed_width = 0;
static int level_passed_height = 0;

// Level intro and end popups (5 levels)
static unsigned char *level_intro_data[NUM_LEVELS] = {NULL};
static int level_intro_width[NUM_LEVELS] = {0};
static int level_intro_height[NUM_LEVELS] = {0};

static unsigned char *level_end_data[NUM_LEVELS] = {NULL};
static int level_end_width[NUM_LEVELS] = {0};
static int level_end_height[NUM_LEVELS] = {0};

// Platform abstraction
int  platform_init(void);
void platform_shutdown(void);
void clear_screen(void);
void put_pixel(int x, int y, uint16_t color);
void present_frame(void);
void poll_input(int *up, int *down, int *left, int *right, int *quit);

// Forward declaration
static void show_popup_and_wait(unsigned char *popup_data, int popup_width, int popup_height);

// ---------- Shared helpers ----------

static uint16_t rgb_to_rgb565(unsigned char r, unsigned char g, unsigned char b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void init_level(int level_index) {
    if (level_index >= NUM_LEVELS) {
        printf("All levels completed!\n");
        running = 0;
        return;
    }
    
    current_level = level_index;
    total_lanes_current = levels[level_index].total_lanes;
    int num_mbta_pairs = levels[level_index].num_mbta_pairs;
    
    // Initialize MBTA lane distribution
    for (int i = 0; i < total_lanes_current; i++) {
        mbta_lane_indices[i] = 0;
    }
    
    // Randomly place MBTA lane pairs
    if (num_lane_types >= 4 && num_mbta_pairs > 0 && total_lanes_current >= 8) {
        srand(time(NULL) + level_index);  // Different seed per level
        int mbta_pairs_placed = 0;
        int max_attempts = num_mbta_pairs * 10;
        int attempts = 0;
        
        while (mbta_pairs_placed < num_mbta_pairs && attempts < max_attempts) {
            if (total_lanes_current < 8) break;
            
            int start_idx = 2 + (rand() % (total_lanes_current - 7));
            
            int can_place = 1;
            for (int j = start_idx; j <= start_idx + 3; j++) {
                if (mbta_lane_indices[j] != 0) {
                    can_place = 0;
                    break;
                }
            }
            
            if (can_place) {
                mbta_lane_indices[start_idx] = -1;
                mbta_lane_indices[start_idx + 1] = 1;
                mbta_lane_indices[start_idx + 2] = 1;
                mbta_lane_indices[start_idx + 3] = -2;
                mbta_pairs_placed++;
            }
            attempts++;
        }
    }
    
    // Reset character position to bottom start lane
    image_x_pos = (screen_width - img_width) / 2;
    image_y_pos = (total_lanes_current - 1) * LANE_HEIGHT + (LANE_HEIGHT - img_height) / 2;
    if (image_x_pos < 0) image_x_pos = 0;
    
    // Reset camera to show building lane at bottom
    // We want the building lane (at total_lanes_current * LANE_HEIGHT) to be visible at the bottom of screen
    // So camera should start at: (total_lanes_current + 1) * LANE_HEIGHT - screen_height
    camera_y = ((total_lanes_current + 1) * LANE_HEIGHT) - screen_height;
    if (camera_y < -LANE_HEIGHT) camera_y = -LANE_HEIGHT;
    
    // Update which lanes are visible
    first_lane_index = camera_y / LANE_HEIGHT;
    
    // Show level intro popup AFTER setting up the new level
    // This way it displays over the new level's background
    if (level_intro_data[level_index]) {
        show_popup_and_wait(level_intro_data[level_index], 
                           level_intro_width[level_index], 
                           level_intro_height[level_index]);
    }
}

static void draw_lanes_and_sprite(void) {
    clear_screen();
    
    // Draw lanes
    for (int i = -1; i < MAX_VISIBLE_LANES; i++) {
        int lane_index = first_lane_index + i;
        
        // Stop drawing if we're past the building lane after the last level lane
        if (lane_index > total_lanes_current) break;
        
        int lane_world_y = lane_index * LANE_HEIGHT;
        int lane_screen_y = lane_world_y - camera_y;
        
        if (lane_screen_y > screen_height) break;
        if (lane_screen_y + LANE_HEIGHT < 0) continue;
        
        // Choose which lane texture to use
        // Lane structure for each level:
        // Lane -1 (off top): building (if visible)
        // Lane 0: start_lane (beginning)
        // Lane 1: top_lane
        // Lanes 2 to total_lanes_current-3: middle lanes (with potential MBTA)
        // Lane total_lanes_current-2: bottom_lane
        // Lane total_lanes_current-1: start_lane (end)
        // Lane total_lanes_current (off bottom): building (if visible)
        Lane *lane;
        if (lane_index == -1 || lane_index == total_lanes_current) {
            lane = &lane_templates[5];  // building (sandwich lanes)
        } else if (lane_index == 0) {
            lane = &lane_templates[4];  // start_lane (beginning)
        } else if (lane_index == total_lanes_current - 1) {
            lane = &lane_templates[4];  // start_lane (end)
        } else if (lane_index == 1) {
            lane = &lane_templates[2];  // top_lane (after start)
        } else if (lane_index == total_lanes_current - 2) {
            lane = &lane_templates[0];  // bottom_lane (before end start)
        } else if (mbta_lane_indices[lane_index] == 1) {
            lane = &lane_templates[3];  // MBTA_lane
        } else if (mbta_lane_indices[lane_index] == -1) {
            lane = &lane_templates[0];  // bottom_lane (border above MBTA)
        } else if (mbta_lane_indices[lane_index] == -2) {
            lane = &lane_templates[2];  // top_lane (border below MBTA)
        } else {
            lane = &lane_templates[1];  // middle_lane
        }
        
        for (int y = 0; y < lane->height && y < LANE_HEIGHT; y++) {
            int screen_y = lane_screen_y + y;
            if (screen_y < 0 || screen_y >= screen_height) continue;
            
            for (int x = 0; x < lane->width && x < screen_width; x++) {
                int img_idx = (y * lane->width + x) * 3;
                unsigned char r = lane->data[img_idx];
                unsigned char g = lane->data[img_idx + 1];
                unsigned char b = lane->data[img_idx + 2];
                
                uint16_t color = rgb_to_rgb565(r, g, b);
                put_pixel(x, screen_y, color);
            }
        }
    }
    
    // Draw sprite at screen position
    int sprite_screen_y = image_y_pos - camera_y;
    
    for (int y = 0; y < img_height; y++) {
        int screen_y = sprite_screen_y + y;
        if (screen_y < 0 || screen_y >= screen_height) continue;
        
        for (int x = 0; x < img_width; x++) {
            int screen_x = image_x_pos + x;
            if (screen_x < 0 || screen_x >= screen_width) continue;
            
            int img_idx = (y * img_width + x) * 4;  // 4 channels: RGBA
            unsigned char r = image_data[img_idx];
            unsigned char g = image_data[img_idx + 1];
            unsigned char b = image_data[img_idx + 2];
            unsigned char a = image_data[img_idx + 3];  // Alpha channel
            
            // Skip transparent pixels
            if (a < 128) continue;  // If mostly transparent, don't draw
            
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

// ---------- Shared popup function ----------

static void show_popup_and_wait(unsigned char *popup_data, int popup_width, int popup_height) {
    if (!popup_data) return;
    
    int waiting = 1;
    int up_press, down_press, left_press, right_press, quit_press;
    
    // Draw the current game state first
    draw_lanes_and_sprite();
    
    // Draw popup centered on screen on top of the game
    int popup_x = (screen_width - popup_width) / 2;
    int popup_y = (screen_height - popup_height) / 2;
    
    for (int y = 0; y < popup_height; y++) {
        int screen_y = popup_y + y;
        if (screen_y < 0 || screen_y >= screen_height) continue;
        
        for (int x = 0; x < popup_width; x++) {
            int screen_x = popup_x + x;
            if (screen_x < 0 || screen_x >= screen_width) continue;
            
            int img_idx = (y * popup_width + x) * 4;  // RGBA
            unsigned char r = popup_data[img_idx];
            unsigned char g = popup_data[img_idx + 1];
            unsigned char b = popup_data[img_idx + 2];
            unsigned char a = popup_data[img_idx + 3];
            
            if (a < 128) continue;  // Skip transparent pixels
            
            uint16_t color = rgb_to_rgb565(r, g, b);
            put_pixel(screen_x, screen_y, color);
        }
    }
    
    present_frame();
    
    // Wait for up button press
    while (waiting && running) {
        quit_press = 0;
        poll_input(&up_press, &down_press, &left_press, &right_press, &quit_press);
        
        if (quit_press) {
            running = 0;
            waiting = 0;
        }
        if (up_press) {
            waiting = 0;  // Exit on up press
        }
        
#ifdef USE_SDL
        SDL_Delay(16);  // ~60 FPS
#else
        usleep(16000);  // 16 ms
#endif
    }
}

// ---------- Shared main ----------

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    int img_channels;
    image_data = stbi_load("assets/guy1.png", &img_width, &img_height, &img_channels, 4);  // Force RGBA
    if (!image_data) {
        fprintf(stderr, "Error: Could not load image 'guy1.png'\n");
        return 1;
    }

    // Load level passed popup
    int popup_channels;
    level_passed_data = stbi_load("assets/level_passed.png", &level_passed_width, &level_passed_height, &popup_channels, 4);

    // Load level intro and end popups for all levels
    for (int i = 0; i < NUM_LEVELS; i++) {
        char intro_filename[64];
        char end_filename[64];
        
        snprintf(intro_filename, sizeof(intro_filename), "assets/lvl%d_intro.png", i + 1);
        snprintf(end_filename, sizeof(end_filename), "assets/lvl%d_end.png", i + 1);
        
        int intro_channels, end_channels;
        level_intro_data[i] = stbi_load(intro_filename, &level_intro_width[i], &level_intro_height[i], &intro_channels, 4);
        if (level_intro_data[i]) {
            printf("Loaded %s (%dx%d)\n", intro_filename, level_intro_width[i], level_intro_height[i]);
        } else {
            fprintf(stderr, "Warning: Could not load %s\n", intro_filename);
        }
        
        level_end_data[i] = stbi_load(end_filename, &level_end_width[i], &level_end_height[i], &end_channels, 4);
        if (level_end_data[i]) {
            printf("Loaded %s (%dx%d)\n", end_filename, level_end_width[i], level_end_height[i]);
        } else {
            fprintf(stderr, "Warning: Could not load %s\n", end_filename);
        }
    }

    // Load lane types (bottom=0, middle=1, top=2, MBTA=3, start=4, building=5)
    const char *lane_files[] = {"assets/bottom_lane.png", "assets/middle_lane.png", "assets/top_lane.png", "assets/MBTA_lane.png", "assets/start_lane.png", "assets/building.png"};
    
    for (int i = 0; i < 6; i++) {
        int w, h;
        lane_templates[i].data = stbi_load(lane_files[i], &w, &h, NULL, 3);
        if (lane_templates[i].data) {
            lane_templates[i].width = w;
            lane_templates[i].height = h;
            num_lane_types++;
        } else {
            fprintf(stderr, "Warning: Could not load %s\n", lane_files[i]);
        }
    }
    
    if (num_lane_types < 3) {
        fprintf(stderr, "Error: Need at least bottom, middle, and top lanes\n");
        stbi_image_free(image_data);
        for (int i = 0; i < num_lane_types; i++) {
            if (lane_templates[i].data) {
                stbi_image_free(lane_templates[i].data);
            }
        }
        return 1;
    }
    
    if (platform_init() != 0) {
        stbi_image_free(image_data);
        for (int i = 0; i < num_lane_types; i++) {
            if (lane_templates[i].data) {
                stbi_image_free(lane_templates[i].data);
            }
        }
        return 1;
    }

    // Initialize first level
    init_level(0);

    while (running) {
        int up, down, left, right, quit = 0;

        poll_input(&up, &down, &left, &right, &quit);
        if (quit) {
            running = 0;
        }

        // Move character in world space
        // Vertical movement: only in lane increments (34 pixels)
        if (up)    image_y_pos -= MOVE_STEP;
        if (down)  image_y_pos += MOVE_STEP;
        
        // Horizontal movement: left and right (no camera tracking horizontally)
        if (left)  image_x_pos -= MOVE_STEP;
        if (right) image_x_pos += MOVE_STEP;

        // Clamp vertical movement within lane bounds
        if (image_y_pos < 0) image_y_pos = 0;  // Can't go below lane 0
        if (image_y_pos > (total_lanes_current - 1) * LANE_HEIGHT) 
            image_y_pos = (total_lanes_current - 1) * LANE_HEIGHT;  // Can't go above top lane
        
        // Check if player reached the top lane (lane 0)
        int current_lane = image_y_pos / LANE_HEIGHT;
        if (current_lane == 0) {
            // Level completed!
            // Show level end popup
            if (level_end_data[current_level]) {
                show_popup_and_wait(level_end_data[current_level], 
                                   level_end_width[current_level], 
                                   level_end_height[current_level]);
            }
            
            // Move to next level
            if (running) {
                init_level(current_level + 1);
            }
            continue;  // Skip rest of loop, start fresh frame
        }
        
        // Clamp horizontal movement only
        if (image_x_pos < 0) image_x_pos = 0;
        if (image_x_pos > screen_width - img_width)
            image_x_pos = screen_width - img_width;

        // Update camera to follow player (keep player in middle of screen when moving up)
        int target_screen_y = screen_height / 2;  // Middle of screen
        camera_y = image_y_pos - target_screen_y;
        
        // Clamp camera so it doesn't go past the edges (including building lanes)
        // Allow camera to show building lane at top (lane -1)
        if (camera_y < -LANE_HEIGHT) camera_y = -LANE_HEIGHT;  // Don't scroll past building lane at top
        // Allow camera to show building lane at bottom (lane total_lanes_current)
        int max_camera_y = ((total_lanes_current + 1) * LANE_HEIGHT) - screen_height;
        if (camera_y > max_camera_y) camera_y = max_camera_y;  // Don't scroll past building lane at bottom

        // Update which lanes are visible
        first_lane_index = camera_y / LANE_HEIGHT;

        draw_lanes_and_sprite();
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
    
    if (level_passed_data) {
        stbi_image_free(level_passed_data);
        level_passed_data = NULL;
    }
    
    for (int i = 0; i < NUM_LEVELS; i++) {
        if (level_intro_data[i]) {
            stbi_image_free(level_intro_data[i]);
            level_intro_data[i] = NULL;
        }
        if (level_end_data[i]) {
            stbi_image_free(level_end_data[i]);
            level_end_data[i] = NULL;
        }
    }
    
    for (int i = 0; i < num_lane_types; i++) {
        if (lane_templates[i].data) {
            stbi_image_free(lane_templates[i].data);
        }
    }

    return 0;
}

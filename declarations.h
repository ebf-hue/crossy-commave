#ifndef DECLARATIONS_H
#define DECLARATIONS_H

#include <stdint.h>

//DEFINITIONS -------------------------------------------------

// Lane management
#define LANE_HEIGHT 34
#define MAX_VISIBLE_LANES 10  // screen_height/LANE_HEIGHT + buffer
#define NUM_MBTA_LANES 3  // Number of MBTA lanes to randomly place
#define NUM_LEVELS 5
#define MAX_TOTAL_LANES 35

//car sprites
#define NUM_CAR_SPRITES 10 // number of different sprite pngs
#define MAX_CARS 64

//special vehicles
#define MAX_SPECIAL_VEHICLES 32

// player movement
#define MOVE_STEP 34 //equal to lane_height
#define VERTICAL_MOVE_DELAY 6 // min number of frames between steps up or down

// level timing
#define LEVEL_START_DELAY 30 // min number of frames before user can move after popup appears

//gpio definition
#define GPIO_BTN0 26 //up
#define GPIO_BTN1 46 //down
#define GPIO_BTN2 47 //left 
#define GPIO_BTN3 27 //right
#define GPIO_PATH "/sys/class/gpio"

//Platform asbtraction-----------------------------------------------------------
int  platform_init(void);
void platform_shutdown(void);
void clear_screen(void);
void put_pixel(int x, int y, uint16_t color);
void present_frame(void);
void poll_input(int *up, int *down, int *left, int *right, int *quit);


//-----------------------------------------------------------

// general
extern volatile int running;

// player sprite
extern unsigned char *image_data;
extern int img_width; 
extern int img_height;
extern int image_x_pos;
extern int image_y_pos;
extern int player_facing_left;

// Cars
typedef struct {
    int active;
    int x;
    int y;
    int speed; // px per frame
    int dir; // +1 = right, -1 = left
    int lane_index; // which lane this car belongs to
    int sprite_index; // which car sprite (color)
} Car;

extern int car_speed;
extern unsigned char* car_data[NUM_CAR_SPRITES];
extern int car_width;
extern int car_height;

// initialize cars array
extern Car cars[MAX_CARS];
extern int frame_counter; // for spawn timing

// special vehicles  (bus, bike, scooter)
typedef enum {
    BUS = 0,
    BIKE,
    SCOOTER,
    TYPE_COUNT
} SpecialType;

typedef struct {
    int active;
    int x, y;
    int speed;
    int dir;
    int lane_index;
    SpecialType type;
} SpecialVehicle;

extern SpecialVehicle specials[MAX_SPECIAL_VEHICLES];
extern int special_speed[TYPE_COUNT];
extern int special_frame_counter;

// special sprites
extern unsigned char* special_data[TYPE_COUNT];
extern int special_w[TYPE_COUNT];
extern int special_h[TYPE_COUNT];

// train sprite
extern unsigned char* train_data;
extern int train_width;
extern int train_height;
extern const int TRAIN_SPEED; // train speed is constant

typedef struct {
    int active;
    int lane_index;
    int x;
    int y;
    int dir;        // -1 = left, 1 = right
    int moving;     // 0 = parked, 1 = moving
} Train;

extern Train trains[MAX_TOTAL_LANES]; // there can only be max one train per mbta lane

// screen size
extern int screen_width;
extern int screen_height;

// Level definitions
typedef struct {
    int total_lanes;
    int num_mbta_pairs;
} LevelConfig;

extern LevelConfig levels[NUM_LEVELS];

typedef struct {
    unsigned char *data;
    int width;
    int height;
} Lane;

extern Lane lane_templates[6];
extern int num_lane_types;
extern Lane level_top_building[NUM_LEVELS];     // Top building for each level
extern Lane level_bottom_building[NUM_LEVELS];  // Bottom building for each level
extern int camera_y;  // Camera offset in world space
extern int first_lane_index;  // Which lane is at the top
extern int mbta_lane_indices[35];  // Support up to 35 lanes max
extern int current_level;
extern int total_lanes_current;

// store traffic direction for each lane
extern int lane_direction[MAX_TOTAL_LANES]; // +1 = right, -1 = left

// Level passed popup
extern unsigned char *level_passed_data;
extern int level_passed_width;
extern int level_passed_height;

// Level intro and end popups (5 levels)
extern unsigned char *level_intro_data[NUM_LEVELS];
extern int level_intro_width[NUM_LEVELS];
extern int level_intro_height[NUM_LEVELS];

extern unsigned char *level_end_data[NUM_LEVELS];
extern int level_end_width[NUM_LEVELS];
extern int level_end_height[NUM_LEVELS];

#endif
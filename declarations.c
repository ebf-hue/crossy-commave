#include "declarations.h"

// ---------- Variable definitions ----------

// general
volatile int running = 1;

// player sprite
unsigned char *image_data = 0;
int img_width = 0, img_height = 0;
int image_x_pos = 0;
int image_y_pos = 0;
int player_facing_left = 0; // 1 for left, 0 for right

// car sprites
int car_speed = 3; // default 2, set in init_level (increases with level)
unsigned char* car_data[NUM_CAR_SPRITES] = {0};
int car_width = 0, car_height = 0;

// initialize cars array
Car cars[MAX_CARS];
int frame_counter = 0; // for spawn timing

SpecialVehicle specials[MAX_SPECIAL_VEHICLES];
int special_speed[TYPE_COUNT] = {0};
int special_frame_counter;
// special sprites
unsigned char* special_data[TYPE_COUNT] = {0};
int special_w[TYPE_COUNT] = {0};
int special_h[TYPE_COUNT] = {0};

// train sprite
unsigned char* train_data = 0;
int train_width = 0, train_height = 0;
const int TRAIN_SPEED = 2; // train speed is constant

// there can only be max one train per mbta lane
Train trains[MAX_TOTAL_LANES];

// screen size
int screen_width = 480;
int screen_height = 272;

LevelConfig levels[NUM_LEVELS] = {
    {12, 1},   // Level 1: 10 game lanes + 2 start lanes, 1 MBTA pair
    {17, 2},   // Level 2: 15 game lanes + 2 start lanes, 2 MBTA pairs
    {22, 3},   // Level 3: 20 game lanes + 2 start lanes, 3 MBTA pairs
    {27, 4},   // Level 4: 25 game lanes + 2 start lanes, 4 MBTA pairs
    {32, 5}    // Level 5: 30 game lanes + 2 start lanes, 5 MBTA pairs
};

Lane lane_templates[6];
int num_lane_types = 0;
Lane level_top_building[NUM_LEVELS];     // Top building for each level
Lane level_bottom_building[NUM_LEVELS];  // Bottom building for each level
int camera_y = 0;  // Camera offset in world space
int first_lane_index = 0;  // Which lane is at the top
int mbta_lane_indices[35];  // Support up to 35 lanes max
int current_level = 0;
int total_lanes_current = 0;

// store traffic direction for each lane
int lane_direction[MAX_TOTAL_LANES]; // +1 = right, -1 = left

// Level passed popup
unsigned char *level_passed_data = 0;
int level_passed_width = 0;
int level_passed_height = 0;

// Level intro and end popups (5 levels)
unsigned char *level_intro_data[NUM_LEVELS] = {0};
int level_intro_width[NUM_LEVELS] = {0};
int level_intro_height[NUM_LEVELS] = {0};

unsigned char *level_end_data[NUM_LEVELS] = {0};
int level_end_width[NUM_LEVELS] = {0};
int level_end_height[NUM_LEVELS] = {0};

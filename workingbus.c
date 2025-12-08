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

// general
static volatile int running = 1;
#define VERTICAL_MOVE_DELAY 6 // min number of frames between steps up or down
static int vertical_move_cooldown = 0;
#define LEVEL_START_DELAY 30 // min number of frames before user can move after popup appears
static int level_start_cooldown = 0;

// Lane management
#define LANE_HEIGHT 34
#define MAX_VISIBLE_LANES 10  // screen_height/LANE_HEIGHT + buffer
#define NUM_MBTA_LANES 3  // Number of MBTA lanes to randomly place
#define NUM_LEVELS 5
#define MAX_TOTAL_LANES 35

// player sprite
static unsigned char *image_data = NULL;
static int img_width = 0, img_height = 0;
static int image_x_pos = 0;
static int image_y_pos = 0;
static int player_facing_left = 0; // 1 for left, 0 for right

// car sprites
#define NUM_CAR_SPRITES 10 // number of different sprite pngs
static int car_speed = 2; // default 2, set in init_level (increases with level)
static unsigned char* car_data[NUM_CAR_SPRITES] = {0};
static int car_width = 0, car_height = 0;
#define MAX_CARS 64
typedef struct {
    int active;
    int x;
    int y;
    int speed; // px per frame
    int dir; // +1 = right, -1 = left
    int lane_index; // which lane this car belongs to
    int sprite_index; // which car sprite (color)
} Car;
// initialize cars array
static Car cars[MAX_CARS];
static int frame_counter = 0; // for spawn timing
static void spawn_car_in_lane(int, int);

// special vehicles  (bus, bike, scooter)
#define MAX_SPECIAL_VEHICLES 32
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

static SpecialVehicle specials[MAX_SPECIAL_VEHICLES];
static int special_speed[TYPE_COUNT] = {0};
static int special_frame_counter;
// special sprites
static unsigned char* special_data[TYPE_COUNT] = {0};
static int special_w[TYPE_COUNT] = {0};
static int special_h[TYPE_COUNT] = {0};

// train sprite
static unsigned char* train_data = NULL;
static int train_width = 0, train_height = 0;
static const int TRAIN_SPEED = 2; // train speed is constant
typedef struct {
    int active;
    int lane_index;
    int x;
    int y;
    int dir;        // -1 = left, 1 = right
    int moving;     // 0 = parked, 1 = moving
} Train;
// there can only be max one train per mbta lane
static Train trains[MAX_TOTAL_LANES];

// screen size
static int screen_width = 480;
static int screen_height = 272;

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

// store traffic direction for each lane
static int lane_direction[MAX_TOTAL_LANES]; // +1 = right, -1 = left

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

// helper function to remove active cars
static void reset_cars(void) {
    for (int i = 0; i < MAX_CARS; i++) {
        cars[i].active = 0;
    }
}
// helper function to remove active trains
static void reset_trains(void) {
    for (int i = 0; i < MAX_TOTAL_LANES; i++) {
        trains[i].active = 0;
    }
}
// helper function to remove active special vehicles
static void reset_specials(void) {
    for (int i = 0; i < MAX_SPECIAL_VEHICLES; i++) {
        specials[i].active = 0;
    }
}

// function to spawn a special vehicle (eg bus)
static void spawn_special_in_lane(int lane_index, int dir) {
    // for now just use bus
    SpecialType type = BUS;   

    // TODO: add other type candidates:
    // SpecialType candidates[VEH_TYPE_COUNT];
    // int n = 0;
    // for (int t = 0; t < VEH_TYPE_COUNT; t++) {
    //     if (special_data[t]) candidates[n++] = t;
    // }
    // if (n == 0) return;
    // type = candidates[rand() % n];

    int w = special_w[type];
    int h = special_h[type];
    if (!special_data[type] || w <= 0 || h <= 0) return;

    // simple proximity check vs. other specials in same lane
    const int prox_gap = w;
    for (int i = 0; i < MAX_SPECIAL_VEHICLES; i++) {
        if (!specials[i].active) continue;
        if (specials[i].lane_index != lane_index) continue;

        if (dir > 0) {
            if (specials[i].x > -prox_gap && specials[i].x < prox_gap) {
                return;
            }
        } else {
            if (specials[i].x > screen_width - prox_gap &&
                specials[i].x < screen_width + prox_gap) {
                return;
            }
        }
    }

    // also proximity check against cars in this lane
    for (int i = 0; i < MAX_CARS; i++) {
        if (!cars[i].active) continue;
        if (cars[i].lane_index != lane_index) continue;

        int cx = cars[i].x;

        if (dir > 0) {
            if (cx > -prox_gap && cx < prox_gap) {
                return;
            }
        } else {
            if (cx > screen_width - prox_gap &&
                cx < screen_width + prox_gap) {
                return;
            }
        }
    }

    // find free slot
    for (int i = 0; i < MAX_SPECIAL_VEHICLES; i++) {
        if (!specials[i].active) {
            specials[i].active     = 1;
            specials[i].lane_index = lane_index;
            specials[i].dir        = dir;
            specials[i].type       = type;
            specials[i].speed      = special_speed[type];

            specials[i].y = lane_index * LANE_HEIGHT + ((LANE_HEIGHT - h) / 2);

            if (dir > 0) specials[i].x = -w;
            else specials[i].x = screen_width;

            return;
        }
    }
}

// function to update special vehicles
static void update_specials(void) {
    // move existing ones
    for (int i = 0; i < MAX_SPECIAL_VEHICLES; i++) {
        if (!specials[i].active) continue;

        SpecialVehicle* sv = &specials[i];
        int w = special_w[sv->type];

        sv->x += sv->dir * sv->speed;
        // check bounds
        if (sv->x > screen_width || sv->x < -w) {
            sv->active = 0;
        }
    }

    // spawn timing
    special_frame_counter++;
    if (special_frame_counter > 1000000) special_frame_counter = 0;

    const int special_interval = 100;  
    if (special_frame_counter % special_interval != 0) return;

    int index_min = 2;
    int index_max = total_lanes_current - 3;
    if (index_max <= index_min) return;

    // only on non-MBTA road lanes, like cars
    for (int attempts = 0; attempts < 3; attempts++) {
        int lane = index_min + rand() % (index_max - index_min + 1);
        if (mbta_lane_indices[lane] == 1) continue;  // skip MBTA rails

        int dir = lane_direction[lane];
        spawn_special_in_lane(lane, dir);
        break;
    }
}



static void init_level(int level_index) {
    if (level_index >= NUM_LEVELS) {
        printf("All levels completed!\n");
        running = 0;
        return;
    }
    
    srand(time(NULL) + level_index);  // Different seed per level
    frame_counter = 0;                // reset car spawn timing
    
    current_level = level_index;
    total_lanes_current = levels[level_index].total_lanes;
    int num_mbta_pairs = levels[level_index].num_mbta_pairs;

    // scale speed with level
    car_speed = 2 + level_index;
    special_speed[BUS] = car_speed - 1; // a little slower than cars
    //special_speed[BUS] = car_speed; // a little slower than cars

    // assign random directions to each lane
    for (int i = 0; i < total_lanes_current; i++) {
        lane_direction[i] = (rand() & 1) ? 1 : -1;
    }
    
    // Initialize MBTA lane distribution
    for (int i = 0; i < total_lanes_current; i++) {
        mbta_lane_indices[i] = 0;
    }

    // reset this level's cars
    reset_cars();
    // reset this level's trains
    reset_trains();
    // reset this level's special vehicles
    reset_specials();
    
    // Randomly place MBTA lane pairs
    if (num_lane_types >= 4 && num_mbta_pairs > 0 && total_lanes_current >= 8) {
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
                mbta_lane_indices[start_idx + 1] = 1; // top
                mbta_lane_indices[start_idx + 2] = 1; // bottom
                mbta_lane_indices[start_idx + 3] = -2;

                // configure trains
                // top
                Train* train_top = &trains[start_idx + 1];
                train_top->active = 1;
                train_top->lane_index = start_idx + 1;
                train_top->moving = rand() & 1; // random 0 for parked or 1 for moving
                train_top->dir = -1;            // top train always faces left
                train_top->y = (start_idx + 1) * LANE_HEIGHT + ((LANE_HEIGHT - train_height) / 2); // center vertically
                // if moving, start off screen
                if (train_top->moving) {
                    // also give every moving train a random start delay distance
                    int offset = rand() % screen_width;
                    train_top->x = screen_width + offset; // offscreen right
                } else {
                    // if parked, start at a random x on screen
                    train_top->x = rand() % (screen_width - train_width);
                }

                // bottom
                Train* train_bottom = &trains[start_idx + 2];
                train_bottom->active = 1;
                train_bottom->lane_index = start_idx + 2;
                train_bottom->moving = rand() & 1; // random 0 for parked or 1 for moving
                train_bottom->dir = 1;            // bottom train always faces right
                train_bottom->y = (start_idx + 2) * LANE_HEIGHT + ((LANE_HEIGHT - train_height) / 2); // center vertically
                // if moving, start off screen
                if (train_bottom->moving) {
                    // also give every moving train a random start delay distance
                    int offset = rand() % screen_width;
                    train_bottom->x = -train_width - offset; // offscreen left
                } else {
                    // if parked, start at a random x on screen
                    train_bottom->x = rand() % (screen_width - train_width);
                }

                mbta_pairs_placed++;
            }
            attempts++;
        }
    }

    // ksenia-proof: start with some cars so that roads aren't empty
    int initial_cars_max = current_level + 6;
    int spawned = 0;

    while (spawned < initial_cars_max) {
        int lane = 2 + rand() % (total_lanes_current - 4);
        // skip mbta
        if (mbta_lane_indices[lane] == 1) continue;
        int dir = lane_direction[lane];
        // first spawn the car normally
        spawn_car_in_lane(lane, dir);
        // then move it to a random x
        for (int i = 0; i < MAX_CARS; i++) {
            if (cars[i].active && cars[i].lane_index == lane) {
                cars[i].x = rand() % (screen_width - car_width);
                break;
            }
        }
        spawned++;
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

    // set cooldown so that player doesn't accidentally close the popup by moving upwards
    level_start_cooldown = LEVEL_START_DELAY;
    
    // Show level intro popup AFTER setting up the new level
    // This way it displays over the new level's background
    if (level_intro_data[level_index]) {
        show_popup_and_wait(level_intro_data[level_index], 
                           level_intro_width[level_index], 
                           level_intro_height[level_index]);
    }
}

static void spawn_car_in_lane(int lane_index, int dir) {
    // first make sure we're not too close to other cars
    const int prox_gap = car_width;

    // check proximity to other cars
    for (int i = 0; i < MAX_CARS; i++) {
        // skip inactive cars
        if (!cars[i].active) continue;
        // find the lane in question
        if (cars[i].lane_index != lane_index) continue;

        // check left edge proximity
        if (dir > 0) {
            if (cars[i].x > -prox_gap && cars[i].x < prox_gap) {
                return; // skip bc too close
            }
        }
        // check right edge proximity
        else {
            if (cars[i].x > screen_width - prox_gap && cars[i].x <= screen_width + prox_gap) {
                return; // skip bc too close
            }
        }
    }

    // check proximity to special vehicles
    for (int i = 0; i < MAX_SPECIAL_VEHICLES; i++) {
        if (!specials[i].active) continue;
        if (specials[i].lane_index != lane_index) continue;

        int w = special_w[specials[i].type];
        int sx = specials[i].x;

        if (dir > 0) {
            if (sx > -w && sx < prox_gap) {
                return;
            }
        } else {
            if (sx > screen_width - prox_gap && sx < screen_width + w) {
                return;
            }
        }
    }

    // use next free slot
    for (int i = 0; i < MAX_CARS; i++) {
        // mark as active and set lane, direction, and speed
        if (!cars[i].active) {
            cars[i].active = 1;
            cars[i].lane_index = lane_index;
            cars[i].dir = dir;
            cars[i].speed = car_speed;

            // randomly pick sprite (color)
            cars[i].sprite_index = rand() % NUM_CAR_SPRITES;

            // center the car vertically in this lane
            cars[i].y = lane_index * LANE_HEIGHT + ((LANE_HEIGHT - car_height) / 2);

            // start offscreen on either side
            if (dir > 0) {
                cars[i].x = -car_width;
            } else {
                cars[i].x = screen_width;
            }
            return;
        }
    }
    // else, no free slots and do nothing
}

static void update_cars(void) {
    // update position of existing cars
    for (int i = 0; i < MAX_CARS; i++) {
        if (!cars[i].active) continue;
        cars[i].x += cars[i].dir * cars[i].speed;
        if (cars[i].x > screen_width || cars[i].x < -car_width) {
            cars[i].active = 0;
        }
    }

    // make cars slow down for bus & other special vehicles
    const int tailgate_gap = car_width; // min distance
    for (int i = 0; i < MAX_CARS; i++) {
        if (!cars[i].active) continue;
        Car* c = &cars[i];
        
        int best_dist = screen_width;
        int best_speed = c->speed;
        int best_front_x = 0;
        int found = 0;

        // look for nearest special vehicle ahead in this lane
        for (int s = 0; s < MAX_SPECIAL_VEHICLES; s++) {
            SpecialVehicle* sv = &specials[s];
            if (!specials[s].active) continue;
            if (sv->lane_index != c->lane_index) continue;
            if (sv->dir != c->dir) continue;

            int dist;

            if (c->dir > 0) {
                // moving right: leader ahead has larger x (use its left edge)
                if (sv->x <= c->x) continue;
                dist = sv->x - c->x;
                if (dist < best_dist) {
                    best_dist    = dist;
                    best_speed   = sv->speed;
                    best_front_x = sv->x;                     // leader "front" = left edge
                    found        = 1;
                }
            } else {
                // moving left: leader ahead is to the LEFT; use its FRONT = right edge
                int leader_front = sv->x + special_w[sv->type];  // bus right edge
                if (leader_front >= c->x) continue;              // must be strictly ahead
                dist = c->x - leader_front;                      // gap: follower left - bus front
                if (dist < best_dist) {
                    best_dist    = dist;
                    best_speed   = sv->speed;
                    best_front_x = leader_front;                 // store *front* (right edge)
                    found        = 1;
                }
            }

        }

        // now look for nearest car ahead in this lane
        for (int j = 0; j < MAX_CARS; j++) {
            if (j == i) continue;
            Car* c2 = &cars[j];
            
            if (!c2->active) continue;
            if (c2->lane_index != c->lane_index) continue;
            if (c2->dir != c->dir) continue;

            int dist;

            if (c->dir > 0) {
                // moving right: leader ahead to the right, use left edge
                if (c2->x <= c->x) continue;
                dist = c2->x - c->x;
                if (dist < best_dist) {
                    best_dist    = dist;
                    best_speed   = c2->speed;
                    best_front_x = c2->x;                  // leader "front" = left edge
                    found        = 1;
                }
            } else {
                // moving left: leader ahead to the LEFT; FRONT = right edge
                int leader_front = c2->x + car_width;      // car right edge
                if (leader_front >= c->x) continue;        // must be to the left (ahead)
                dist = c->x - leader_front;
                if (dist < best_dist) {
                    best_dist    = dist;
                    best_speed   = c2->speed;
                    best_front_x = leader_front;           // store *front* (right edge)
                    found        = 1;
                }
            }
        }
        

        // match speed of the slowpoke
        if (found && best_dist < tailgate_gap) {

            if (c->dir > 0) {
                // leader front = left edge; put follower so its right edge touches that:
                // follower_left = leader_left - car_width
                c->x = best_front_x - tailgate_gap;    // tailgate_gap == car_width
            } else {
                // leader front = right edge; put follower so its left edge touches that:
                // follower_left = leader_right
                // but only clamp if next movement would go past the target spot
                int target = best_front_x;
                c->x = best_front_x;
            }

            c->speed = best_speed;
        }
    }

    frame_counter++;
    if (frame_counter > 1000000) frame_counter = 0; // occasional reset

    // spawnable lane range
    int index_min = 2;
    int index_max = total_lanes_current - 3;
    if (index_max <= index_min) return;

    // count spawnable lanes (non-mbta)
    int spawnable_lanes = 0;
    for (int lane = index_min; lane <= index_max; lane++) {
        if (mbta_lane_indices[lane] != 1) spawnable_lanes++;
    }
    if (spawnable_lanes <= 0) return;

    // base: level 0, ~8 lanes → interval ≈ 40
    const int ref_lanes    = 8;
    const int ref_interval = 35;
    float interval_f = (float)ref_interval * (float)ref_lanes / (float)spawnable_lanes;

    // make higher levels busier
    float level_factor = 1.0f + 0.35f * current_level;
    interval_f /= level_factor;

    int spawn_interval = (int)interval_f;
    if (spawn_interval < 2) spawn_interval = 2;

    // spawn?
    if (frame_counter % spawn_interval == 0) {
        for (int attempts = 0; attempts < 3; attempts++) {
            int lane_index = index_min + rand() % (index_max - index_min + 1);
            if (mbta_lane_indices[lane_index] == 1) continue; // skip rail
            int dir = lane_direction[lane_index];
            spawn_car_in_lane(lane_index, dir);
            break;
        }
    }
}

static void update_trains(void) {
    for (int i = 0; i < MAX_TOTAL_LANES; i++) {
        Train* t = &trains[i];
        // skip if parked or not active
        if (!t->active) continue;
        if (!t->moving) continue;
        // update position of moving train
        t->x += t->dir * TRAIN_SPEED;

        // wrap around to stay in this lane forever hehehehahahaHAHAHAAAHHAAHAHAH!
        if (t->dir > 0 && t->x > screen_width) { // if moving right off screen
            t->x = -train_width; // move it back to left side
        } else if (t->dir < 0 && t->x < -train_width) { // if moving left off screen
            t->x = screen_width; // move it back to right side
        }
    }
}

// check for collisions with cars, trains, and special vehicles
static int check_car_collisions(void) {
    // player hitbox
    const int p_margin_x = 4;

    int px = image_x_pos + p_margin_x;
    int py = image_y_pos;
    int pw = img_width - 2 * p_margin_x;
    int ph = img_height;

    // check each car
    for (int i = 0; i < MAX_CARS; i++) {
        // skip inactive ones
        if (!cars[i].active) continue;

        // car hitbox
        int cx = cars[i].x;
        int cy = cars[i].y;
        int cw = car_width;
        int ch = car_height;

        int overlap = (px < cx + cw) &&
            (px + pw > cx) &&
            (py < cy + ch) &&
            (py + ph > cy);
        // collision detected
        if (overlap) return 1;
    }

    // add extra margin for train hitbox
    const int t_margin_x = 4;

    // check each train
    for (int i = 0; i < MAX_TOTAL_LANES; i++) {
        // skip inactive ones
        if (!trains[i].active) continue;

        // train hitbox
        int tx = trains[i].x + t_margin_x;
        int ty = trains[i].y;
        int tw = train_width - 2 * t_margin_x;
        int th = train_height;

        int overlap = (px < tx + tw) &&
            (px + pw > tx) &&
            (py < ty + th) &&
            (py + ph > ty);
        // collision detected
        if (overlap) return 1;
    }

    // check special vehicles (bus, bike, scooter)
    for (int i = 0; i < MAX_SPECIAL_VEHICLES; i++) {
        if (!specials[i].active) continue;
        SpecialVehicle* sv = &specials[i];
        int w = special_w[sv->type];
        int h = special_h[sv->type];

        int vx = sv->x;
        int vy = sv->y;

        int overlap = (px < vx + w) &&
                      (px + pw > vx) &&
                      (py < vy + h) &&
                      (py + ph > vy);
        if (overlap) return 1;
    }

    // no collisions
    return 0;
}

static void draw_cars(void) {
    for (int i = 0; i < MAX_CARS; i++) {
        // skip inactive cars
        if (!cars[i].active) continue;

        // get the specific color sprite
        unsigned char* sprite = car_data[cars[i].sprite_index];
       
        // loop through every pixel
        int sprite_screen_y = cars[i].y - camera_y;
        for (int y = 0; y < car_height; y++) {
            int screen_y = sprite_screen_y + y;
            // skip out of frame
            if (screen_y < 0 || screen_y >= screen_height) continue;

            for (int x = 0; x < car_width; x++) {
                int screen_x = cars[i].x + x;
                // skip out of frame
                if (screen_x < 0 || screen_x >= screen_width) continue;

                // if car is going left, flip image
                int src_x = x;
                if (cars[i].dir < 0) {
                    src_x = car_width - 1 - x;
                }

                int img_idx = (y * car_width + src_x) * 4;
                unsigned char r = sprite[img_idx];
                unsigned char g = sprite[img_idx + 1];
                unsigned char b = sprite[img_idx + 2];
                unsigned char a = sprite[img_idx + 3];
                
                if (a < 128) continue;
                uint16_t color = rgb_to_rgb565(r, g, b);
                put_pixel(screen_x, screen_y, color);
            }
        }
    }
}

static void draw_trains(void) {
    for (int i = 0; i < MAX_TOTAL_LANES; i++) {
        Train* t = &trains[i];
        // skip inactive ones
        if (!t->active) continue;

		int sprite_screen_y = t->y - camera_y;
        for (int y = 0; y < train_height; y++) {
            int screen_y = sprite_screen_y + y;
            if (screen_y < 0 || screen_y >= screen_height) continue;

            for (int x = 0; x < train_width; x++) {
                int screen_x = t->x + x;
                if (screen_x < 0 || screen_x >= screen_width) continue;

                // flip based on direction just like others
                int src_x = x;
                if (t->dir > 0) {
                    src_x = train_width - 1 - x;
                }

                int img_idx = (y * train_width + src_x) * 4;
                unsigned char r = train_data[img_idx];
                unsigned char g = train_data[img_idx + 1];
                unsigned char b = train_data[img_idx + 2];
                unsigned char a = train_data[img_idx + 3];

                if (a < 128) continue;
                uint16_t color = rgb_to_rgb565(r, g, b);
                put_pixel(screen_x, screen_y, color);
            }
        }
    }
}

static void draw_specials(void) {
    for (int i = 0; i < MAX_SPECIAL_VEHICLES; i++) {
        // skip inactive
        if (!specials[i].active) continue;

        SpecialVehicle* sv = &specials[i];
        unsigned char* tex = special_data[sv->type];
        int w = special_w[sv->type];
        int h = special_h[sv->type];
        if (!tex) continue;

        int sprite_screen_y = sv->y - camera_y;

        for (int y = 0; y < h; y++) {
            int screen_y = sprite_screen_y + y;
            if (screen_y < 0 || screen_y >= screen_height) continue;

            for (int x = 0; x < w; x++) {
                int screen_x = sv->x + x;
                if (screen_x < 0 || screen_x >= screen_width) continue;

                int src_x = x;
                if (sv->dir > 0) {
                    src_x = w - 1 - x;
                }

                int img_idx = (y * w + src_x) * 4;
                unsigned char r = tex[img_idx];
                unsigned char g = tex[img_idx + 1];
                unsigned char b = tex[img_idx + 2];
                unsigned char a = tex[img_idx + 3];

                if (a < 128) continue;
                uint16_t color = rgb_to_rgb565(r, g, b);
                put_pixel(screen_x, screen_y, color);
            }
        }
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
        if (lane_index < -1) continue; // quick bounds check
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

    // draw cars
    draw_cars();

    // draw trains
    draw_trains();
    
    // draw special vehicles
    draw_specials();
    
    // Draw player sprite at screen position
    int sprite_screen_y = image_y_pos - camera_y;
    
    for (int y = 0; y < img_height; y++) {
        int screen_y = sprite_screen_y + y;
        if (screen_y < 0 || screen_y >= screen_height) continue;
        
        for (int x = 0; x < img_width; x++) {
            int screen_x = image_x_pos + x;
            if (screen_x < 0 || screen_x >= screen_width) continue;
            
            // flip left or right
            int src_x = x;
            if (player_facing_left) {
                src_x = img_width - 1 - x;
            }

            int img_idx = (y * img_width + src_x) * 4;  // 4 channels: RGBA
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
    // first set cooldown so user doesn't accidentally close it too quickly
    level_start_cooldown = LEVEL_START_DELAY;
    while (waiting && running) {
        quit_press = 0;
        poll_input(&up_press, &down_press, &left_press, &right_press, &quit_press);
        
        if (quit_press) {
            running = 0;
            waiting = 0;
        }
        // check that the cooldown has passed
        if (up_press && level_start_cooldown == 0) {
            waiting = 0;  // Exit on up press
        }
        // decrement cooldown
        if (level_start_cooldown > 0) level_start_cooldown--;
        
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

    // load player sprite
    int img_channels;
    image_data = stbi_load("assets/guy1.png", &img_width, &img_height, &img_channels, 4);  // Force RGBA
    if (!image_data) {
        fprintf(stderr, "Error: Could not load player sprite\n");
        return 1;
    }

    // load car sprites
    const char* car_sprites[NUM_CAR_SPRITES] = {
        "assets/car1.png", // red
        "assets/car_lightblue.png",
        "assets/car_mediumblue.png",
        "assets/car_darkblue.png",
        "assets/car_lightgreen.png",
        "assets/car_darkgreen.png",
        "assets/car_purple.png",
        "assets/car_white.png",
        "assets/car_grey.png",
        "assets/car_black.png"
    };

    int car_channels;
    for (int i = 0; i < NUM_CAR_SPRITES; i++) {
        int w, h;
        car_data[i] = stbi_load(car_sprites[i], &w, &h, &car_channels, 4);
        if (!car_data[i]) {
            fprintf(stderr, "Error: Could not load car sprite %s\n", car_sprites[i]);
            stbi_image_free(image_data);
            return 1;
        }
        if (i == 0) {
            car_width = w;
            car_height = h;
        }
    }

    // load train sprite
    int train_channels;
    //train_data = stbi_load("assets/TT.png", &train_width, &train_height, &train_channels, 4);
    train_data = stbi_load("assets/T2.png", &train_width, &train_height, &train_channels, 4);
    if (!train_data) {
        fprintf(stderr, "Error: Could not load train sprite\n");
        stbi_image_free(image_data);
        stbi_image_free(car_data);
        return 1;
    }

    // load bus sprite
    int special_channels;
    special_data[BUS] = stbi_load("assets/bus2.png", &special_w[BUS], &special_h[BUS], &special_channels, 4);
    if (!special_data[BUS]) {
        fprintf(stderr, "Error: Could not load bus sprite\n");
        stbi_image_free(image_data);
        stbi_image_free(car_data);
        stbi_image_free(train_data);
        return 1;
    }
    // set bus speed
    special_speed[BUS] = car_speed - 1; // a little slower than cars


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
        stbi_image_free(car_data);
        for (int i = 0; i < num_lane_types; i++) {
            if (lane_templates[i].data) {
                stbi_image_free(lane_templates[i].data);
            }
        }
        return 1;
    }
    
    if (platform_init() != 0) {
        stbi_image_free(image_data);
        stbi_image_free(car_data);
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

        // decrement movement cooldown
        if (vertical_move_cooldown > 0) vertical_move_cooldown--;
        // also decrement the level start movement cooldown
        if (level_start_cooldown > 0) level_start_cooldown--;

        // Move character in world space
        // Vertical movement: only in lane increments (34 pixels)
        // only allow movement up/down if enough frames have passed
        // also only allow any movement at all if level start delay has passed
        if (level_start_cooldown == 0) {
            if (vertical_move_cooldown == 0) {
                if (up) {
                    image_y_pos -= MOVE_STEP;
                    vertical_move_cooldown = VERTICAL_MOVE_DELAY;
                }
                else if (down) {
                    image_y_pos += MOVE_STEP;
                    vertical_move_cooldown = VERTICAL_MOVE_DELAY;
                }
            }
            
            // Horizontal movement: left and right (no camera tracking horizontally)
            if (left) {
                image_x_pos -= MOVE_STEP;
                player_facing_left = 1;
            }
            if (right) {
                image_x_pos += MOVE_STEP;
                player_facing_left = 0;
            }
        }

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

        // update car positions
        update_cars();
        // update train positions
        update_trains();
        // update special vehicles positions
        update_specials();

        // check for car collisions
        if (check_car_collisions()) {
            // draw the collision frame
            draw_lanes_and_sprite();
            present_frame();
            // brief delay so user can perceive the collision
        #ifdef USE_SDL
            SDL_Delay(400);     // 400 ms
        #else
            usleep(400000);     // 400 ms
        #endif

            // restart this level
            init_level(current_level);
            continue; // don't draw
        }
        
        draw_lanes_and_sprite();
        present_frame();

#ifdef USE_SDL
        SDL_Delay(16);      // ~60 FPS
#else
        usleep(50000);      // 50 ms
#endif
    }

    platform_shutdown();

    // CLEANUP

    if (image_data) {
        stbi_image_free(image_data);
        image_data = NULL;
    }

    for (int i = 0; i < NUM_CAR_SPRITES; i++) {
        if (car_data[i]) {
            stbi_image_free(car_data[i]);
            car_data[i] = NULL;
        }
    }
    
    for (int i = 0; i < TYPE_COUNT; i++) {
        if (special_data[i]) {
            stbi_image_free(special_data[i]);
            special_data[i] = NULL;
        }
    }
    
    if (train_data) {
        stbi_image_free(train_data);
        train_data = NULL;
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
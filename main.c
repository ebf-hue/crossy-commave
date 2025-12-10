#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "declarations.h"
#include "vehicle.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef USE_SDL
#include <SDL2/SDL.h>
#else
#include <unistd.h>
#endif

//FORWARD DECLARATIONS
static void show_popup_and_wait(unsigned char *popup_data, int popup_width, int popup_height);

//HELPER FUNCTIONS
static uint16_t rgb_to_rgb565(unsigned char r, unsigned char g, unsigned char b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// level initialization (called at the start of each of our 5 predefined levels)
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
    car_speed = 3 + level_index;
    special_speed[BUS] = car_speed - 1; // a little slower than cars

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
    int initial_cars_max = current_level + 4;
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
    
    // reset character position to bottom start lane
    image_x_pos = (screen_width - img_width) / 2;
    image_y_pos = (total_lanes_current - 1) * LANE_HEIGHT + (LANE_HEIGHT - img_height) / 2;
    if (image_x_pos < 0) image_x_pos = 0;
    
    // reset camera to show building lane at bottom
    camera_y = ((total_lanes_current + 1) * LANE_HEIGHT) - screen_height;
    if (camera_y < -LANE_HEIGHT) camera_y = -LANE_HEIGHT;
    
    // update which lanes are visible
    first_lane_index = camera_y / LANE_HEIGHT;

    //show level intro popup AFTER setting up the new level
    if (level_intro_data[level_index]) {
        show_popup_and_wait(level_intro_data[level_index], 
                           level_intro_width[level_index], 
                           level_intro_height[level_index]);
    }
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
    
    // draw lanes
    for (int i = -1; i < MAX_VISIBLE_LANES; i++) {
        int lane_index = first_lane_index + i;
        
        //stop drawing if past the building lane after the last level lane
        if (lane_index > total_lanes_current) break;
        
        int lane_world_y = lane_index * LANE_HEIGHT;
        int lane_screen_y = lane_world_y - camera_y;
        
        if (lane_screen_y > screen_height) break;
        if (lane_screen_y + LANE_HEIGHT < 0) continue;
        
        //LANE ORDER AHH
        Lane *lane;
        if (lane_index < -1) continue;
        
        if (lane_index == -1) {  //FIRST LANE
            lane = &level_top_building[current_level];  // level-specific top building
        } else if (lane_index == total_lanes_current) { //LAST LANE
            lane = &level_bottom_building[current_level];  // level-specific bottom building
        } else if (lane_index == 0) { //bottom sidewalk lane
            lane = &lane_templates[4];
        } else if (lane_index == total_lanes_current - 1) { //second to last lane (sidewalk)
            lane = &lane_templates[5];
        } else if (lane_index == 1) { //road start bottom (blank lower half)
            lane = &lane_templates[2];
        } else if (lane_index == total_lanes_current - 2) { //2 before last lane,  //road top (blank upper half)
            lane = &lane_templates[0];
        } else if (mbta_lane_indices[lane_index] == 1) {  
            lane = &lane_templates[3];
        } else if (mbta_lane_indices[lane_index] == -1) {
            lane = &lane_templates[0];
        } else if (mbta_lane_indices[lane_index] == -2) {
            lane = &lane_templates[2];
        } else {
            lane = &lane_templates[1];
        }
        
        //DRAW THEM
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

            int img_idx = (y * img_width + src_x) * 4;  // 4 rgba channels
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


// LEVEL POPUP FUNCTION
static void show_popup_and_wait(unsigned char *popup_data, int popup_width, int popup_height) {
    if (!popup_data) return;
    
    int waiting = 1;
    int up_press, down_press, left_press, right_press, quit_press;
    
    //draw current game state
    draw_lanes_and_sprite();
    
    // draw popup over it
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
    
    // wait for "up" buttom press
    while (waiting && running) {
        up_press = 0, down_press = 0, left_press = 0, right_press = 0, quit_press = 0;
        poll_input(&up_press, &down_press, &left_press, &right_press, &quit_press);
        
        if (quit_press) {
            running = 0;
            waiting = 0;
        }
        if (up_press) {
            waiting = 0;  // exit on up press
        }

        //different delays funcs for mac and beaglebone
#ifdef USE_SDL
        SDL_Delay(16);  // ~60 FPS
#else
        usleep(16000);
#endif
    }
}

// MAIN ---------------------------------------------------
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

    // load level intro and end popups 
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
    
    //load lane graphics
    const char *lane_files[] = {
        "assets/bottom_lane.png", 
        "assets/middle_lane.png", 
        "assets/top_lane.png", 
        "assets/MBTA_lane.png", 
        "assets/street_top.png",
        "assets/street_bottom.png"
    };
    
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
    
    //load specific building graphics for all levels 
    for (int i = 0; i < NUM_LEVELS; i++) {
        char top_filename[64];
        char bottom_filename[64];
        
        snprintf(top_filename, sizeof(top_filename), "assets/Level%d_top.png", i + 1);
        snprintf(bottom_filename, sizeof(bottom_filename), "assets/Level%d_bottom.png", i + 1);
        
        int w, h;
        level_top_building[i].data = stbi_load(top_filename, &w, &h, NULL, 3);
        if (level_top_building[i].data) {
            level_top_building[i].width = w;
            level_top_building[i].height = h;
            printf("Loaded %s (%dx%d)\n", top_filename, w, h);
        } else {
            fprintf(stderr, "Warning: Could not load %s\n", top_filename);
        }
        
        level_bottom_building[i].data = stbi_load(bottom_filename, &w, &h, NULL, 3);
        if (level_bottom_building[i].data) {
            level_bottom_building[i].width = w;
            level_bottom_building[i].height = h;
            printf("Loaded %s (%dx%d)\n", bottom_filename, w, h);
        } else {
            fprintf(stderr, "Warning: Could not load %s\n", bottom_filename);
        }
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

    // initialize first level
    init_level(0);

    while (running) {
        
        //PREVENT CHAOS
        int up = 0, down = 0, left = 0, right = 0, quit = 0;

        //get direction inputs
        poll_input(&up, &down, &left, &right, &quit);
        if (quit) {
            running = 0;
        }

        //movement only in lane increments (MOVE_STEP = 34 pixels)
        if (up) {
            image_y_pos -= MOVE_STEP;
        }
        if (down) {
            image_y_pos += MOVE_STEP;
        }
        if (left) {
            image_x_pos -= MOVE_STEP;
            player_facing_left = 1;
        }
        if (right) {
            image_x_pos += MOVE_STEP;
            player_facing_left = 0;
        }

        //vertical movement within lane bounds
        if (image_y_pos < 0) image_y_pos = 0;  // Can't go below lane 0
        if (image_y_pos > (total_lanes_current - 1) * LANE_HEIGHT) 
            image_y_pos = (total_lanes_current - 1) * LANE_HEIGHT;  // cant go above second to last lane
        
        // at top lane?
        int current_lane = image_y_pos / LANE_HEIGHT;
        if (current_lane == 0) {
            // Level completed! -> show popup
            if (level_end_data[current_level]) {
                show_popup_and_wait(level_end_data[current_level], 
                                   level_end_width[current_level], 
                                   level_end_height[current_level]);
            }
            
            // next level
            if (running) {
                init_level(current_level + 1);
            }
            continue;
        }
        
        // clamp horizontal movement
        if (image_x_pos < 0) image_x_pos = 0;
        if (image_x_pos > screen_width - img_width)
            image_x_pos = screen_width - img_width;

        // keep player in middle of screen when moving up
        int target_screen_y = screen_height / 2;  // middle of screen
        camera_y = image_y_pos - target_screen_y;
        
        //CAMERA CLAMP VERTICAL
        //show buildings at top
        if (camera_y < -LANE_HEIGHT) camera_y = -LANE_HEIGHT;
        //show buildings lane at bottom
        int max_camera_y = ((total_lanes_current + 1) * LANE_HEIGHT) - screen_height;
        if (camera_y > max_camera_y) camera_y = max_camera_y;
        
        //update cam
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
            SDL_Delay(800);     // 400 ms
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
        usleep(16000);      // 60 fps
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
    
    // Free level-specific building textures
    for (int i = 0; i < NUM_LEVELS; i++) {
        if (level_top_building[i].data) {
            stbi_image_free(level_top_building[i].data);
        }
        if (level_bottom_building[i].data) {
            stbi_image_free(level_bottom_building[i].data);
        }
    }

    return 0;
}

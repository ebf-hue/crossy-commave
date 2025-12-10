#include "vehicle.h"
#include "declarations.h"

// RESET, UPDATE, AND SPAWN FUNCTIONS FOR CARS, TRAINS, AND BUSES

/******** RESET ********/

// remove all active cars
void reset_cars(void) {
    for (int i = 0; i < MAX_CARS; i++) {
        cars[i].active = 0;
    }
}

// remove active trains
void reset_trains(void) { 
    for (int i = 0; i < MAX_TOTAL_LANES; i++) {
        trains[i].active = 0;
    }
}

// remove active special vehicles
void reset_specials(void) { 
    for (int i = 0; i < MAX_SPECIAL_VEHICLES; i++) {
        specials[i].active = 0;
    }
}

/******** UPDATES ********/

// check for collisions with cars, trains, and special vehicles (returns 1 if collision is detected)
int check_car_collisions(void) {
    // player hitbox
    //const int p_margin_x = 4;
    const int p_margin_x = 16;

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

void update_cars(void) {
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
    const int ref_interval = 40;
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

void update_trains(void) {
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


// update the bus positions and roll the dice for a spawn
void update_specials(void) {
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

    const int special_interval = 150;
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


/******** SPAWNING ********/
// spawn different colored cars
void spawn_car_in_lane(int lane_index, int dir) {                                                      
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

// spawn a special vehicle (for now just bus)
void spawn_special_in_lane(int lane_index, int dir) { 
    SpecialType type = BUS;                                                                                   
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
            //specials[i].dir        = dir;
            specials[i].dir        = 1;
            specials[i].type       = type;
            specials[i].speed      = special_speed[type];

            specials[i].y = lane_index * LANE_HEIGHT + ((LANE_HEIGHT - h) / 2);

            if (dir > 0) specials[i].x = -w;
            else specials[i].x = screen_width;

            return;
        }
    }
}




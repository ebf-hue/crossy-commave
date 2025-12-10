// vehicle.h -- declarations for spawning, updating, and resetting cars, train, and bus

#include "declarations.h"
#include <stdlib.h>

#ifndef VEHICLE_H
#define VEHICLE_H

/******** SPAWNING ********/
// spawn each vehicle type in random lanes
void spawn_car_in_lane(int, int);
void spawn_special_in_lane(int, int);

/******** UPDATES ********/
// update helpers to manage vehicle position and spawning frequency
int check_car_collisions(void);
void update_cars(void);
void update_trains(void);
void update_specials(void);

/******** RESET ********/
// these functions remove all active instances of the vehicle type
void reset_cars(void);
void reset_trains(void);
void reset_specials(void);

#endif

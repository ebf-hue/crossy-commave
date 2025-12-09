# Crossy Commonwealth Avenue #
## A fun and fast-paced 2D game for both Beaglebone and your laptop
Inspired by Crossy Road, this game is all about moving your player up, down, left, or right to traverse a series of dangerous traffic lanes. Dodge the cars, MBTA train, and BU bus while you run between BU-themed buildings, all with custom pixel art!

The game includes custom graphics for 5 predefined levels. Difficulty gradually increases as the player passes each level. With our modular design, infinite levels can be generated according to configurable parameters like vehicle speed or number of lanes. 

## How to play ##
Compile for your laptop with "make laptop" or for Beaglebone with "make beaglebone", then simply run the executable with ./sprite_test or ./sprite_fasterer. Press ctrl-C to quit the game.

On laptop, use the arrow keys to move up, down, left, and right. Press the up arrow to start and move between levels.
On Beaglebone, use the four GPIO pushbuttons to move up, down, left, and right. Press the top button to start and move between levels.

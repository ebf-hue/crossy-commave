# Crossy Commonwealth Avenue #
## A fun and fast-paced 2D game for Beaglebone Black
Inspired by Crossy Road, this game is all about moving your player up, down, left, or right to traverse a series of dangerous traffic lanes. Dodge the cars, MBTA train, and BU bus while you run between BU-themed buildings, all with custom pixel art!

The game includes custom graphics for 5 predefined levels featuring iconic spots along the Boston University campus. Difficulty gradually increases as the player passes each level. With our modular design, infinite levels can be generated according to configurable parameters like vehicle speed or number of lanes. 

## How to run ##
Get the source code by either downloading and extracting the zip file or cloning the repository. Compile for your laptop with "make laptop" or for Beaglebone with "make beaglebone", then simply run the executable with ./sprite_test or ./sprite_fasterer. The executable and the /assets folder must be in the same directory.

To play on Beaglebone, connect the 3V3 pin through the up, right, left, and down buttons to GPIO pins 26, 27, 47, and 46, respectively, with 1k resistors to GND at each GPIO pin.

## How to play ##
- On laptop, use the arrow keys to move up, down, left, and right. Press the up arrow to start and move between levels.
- On Beaglebone, use the four GPIO pushbuttons to move up, down, left, and right. Press the top button to start and move between levels.
- To start the game, move upwards. Your goal is to cross all lanes of traffic without running into any vehicles. Once you reach the top of a level, move upwards to progress to the next level. Win the game by completing all five! Quit at any time by pressing Ctrl-C.

#### Created by Elena Berrios and Ksenia Suglobova.

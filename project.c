/*
 * project.c
 *
 * Main file for the Tetris Project.
 *
 * Author: Peter Sutton. Modified by Max Bo
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdio.h>
#include <stdlib.h>		// For random()

#include "ledmatrix.h"
#include "scrolling_char_display.h"
#include "buttons.h"
#include "serialio.h"
#include "terminalio.h"
#include "score.h"
#include "timer0.h"
#include "game.h"

#define F_CPU 8000000L
#include <util/delay.h>

// Function prototypes - these are defined below (after main()) in the order
// given here
void initialise_hardware(void);
void splash_screen(void);
void new_game(void);
void play_game(void);
void handle_game_over(void);
void handle_new_lap(void);
void update_seven_segment(void);
void convert_joystick(void);
uint8_t is_left(void);
uint8_t is_right(void);
uint8_t is_up(void);
uint8_t is_down(void);

// ASCII code for Escape character
#define ESCAPE_CHAR 27

/////////////////////////////// main //////////////////////////////////
int main(void) {
	// Setup hardware and call backs. This will turn on 
	// interrupts.
	initialise_hardware();
	
	// Show the splash screen message. Returns when display
	// is complete
	splash_screen();
	
	while(1) {
		new_game();
		play_game();
		handle_game_over();
	}
}

void initialise_hardware(void) {
	ledmatrix_setup();
	init_button_interrupts();
	
	// Setup serial port for 19200 baud communication with no echo
	// of incoming characters
	init_serial_stdio(19200,0);
	
	// Set up our main timer to give us an interrupt every millisecond
	init_timer0();
	
	// Turn on global interrupts
	sei();
	
	ADMUX = (1<<REFS0);
	ADCSRA = (1<<ADEN) | (1<<ADPS2) | (1<<ADPS2);
}

void splash_screen(void) {
	// Reset display attributes and clear terminal screen then output a message
	set_display_attribute(TERM_RESET);
	clear_terminal();
	
	hide_cursor();	// We don't need to see the cursor when we're just doing output
	move_cursor(3,3);
	printf_P(PSTR("Tetris"));
	
	move_cursor(3,5);
	set_display_attribute(FG_GREEN);	// Make the text green
	printf_P(PSTR("CSSE2010/7201 Tetris Project by Max Bo"));	
	set_display_attribute(FG_WHITE);	// Return to default colour (White)
	
	// Output the scrolling message to the LED matrix
	// and wait for a push button to be pushed.
	ledmatrix_clear();
	
	// Red message the first time through
	PixelColour colour = COLOUR_RED;
	while(1) {
		set_scrolling_display_text("43926871", colour);
		// Scroll the message until it has scrolled off the 
		// display or a button is pushed. We pause for 130ms between each scroll.
		while(scroll_display()) {
			_delay_ms(130);
			if(button_pushed() != -1) {
				// A button has been pushed
				return;
			}
		}
		// Message has scrolled off the display. Change colour
		// to a random colour and scroll again.
		switch(random()%4) {
			case 0: colour = COLOUR_LIGHT_ORANGE; break;
			case 1: colour = COLOUR_RED; break;
			case 2: colour = COLOUR_YELLOW; break;
			case 3: colour = COLOUR_GREEN; break;
		}
	}
}

void new_game(void) {
	// Initialise the game and display
	init_game();
	
	// Clear the serial terminal
	clear_terminal();
	
	// Initialise the score
	init_score();
	init_cleared_rows();
	
	// Delete any pending button pushes or serial input
	empty_button_queue();
	clear_serial_input_buffer();
}

void play_game(void) {
	uint32_t last_drop_time;
	int8_t button;
	char serial_input, escape_sequence_char;
	uint8_t characters_into_escape_sequence = 0;
	uint8_t paused = 0;
	uint32_t paused_time = 0;

	DDRC = 0xFF;
	DDRD |= 0b10000000; // 7 to output
	DDRD &= 0b10111111; // 6 to input
	
	// I'm putting all the features that need to get kicked off immediately here and not wiped
	// by new_game
	move_cursor(3, 3);
	printf_P(PSTR("Score: %6d"), get_score());
	move_cursor(3, 6);
	printf_P(PSTR("Cleared rows: %6d"), get_cleared_rows());
	print_block_preview();
	
	
	// y, startx, endx
	draw_horizontal_line(4, 30, 30 + BOARD_WIDTH - 1);
	draw_horizontal_line(4 + BOARD_ROWS + 1, 30, 30 + BOARD_WIDTH - 1);
	
	// x, starty, endy
	draw_vertical_line(30 - 1, 4, 4 + BOARD_ROWS + 1);
	draw_vertical_line(30 + BOARD_WIDTH, 4, 4 + BOARD_ROWS + 1);
	
	// Record the last time a block was dropped as the current time -
	// this ensures we don't drop a block immediately.
	last_drop_time = get_clock_ticks();
	
	// We play the game forever. If the game is over, we will break out of
	// this loop. The loop checks for events (button pushes, serial input etc.)
	// and on a regular basis will drop the falling block down by one row.
	while(1) {
		
		// And everything that needs to be called in the main loop
		update_seven_segment();
		convert_joystick();

		// Check for input - which could be a button push or serial input.
		// Serial input may be part of an escape sequence, e.g. ESC [ D
		// is a left cursor key press. We will be processing each character
		// independently and can't do anything until we get the third character.
		// At most one of the following three variables will be set to a value 
		// other than -1 if input is available.
		// (We don't initalise button to -1 since button_pushed() will return -1
		// if no button pushes are waiting to be returned.)
		// Button pushes take priority over serial input. If there are both then
		// we'll retrieve the serial input the next time through this loop
		serial_input = -1;
		escape_sequence_char = -1;
		button = button_pushed();
		
		if(button == -1) {
			// No push button was pushed, see if there is any serial input
			if(serial_input_available()) {
				// Serial data was available - read the data from standard input
				serial_input = fgetc(stdin);
				// Check if the character is part of an escape sequence
				if(characters_into_escape_sequence == 0 && serial_input == ESCAPE_CHAR) {
					// We've hit the first character in an escape sequence (escape)
					characters_into_escape_sequence++;
					serial_input = -1; // Don't further process this character
				} else if(characters_into_escape_sequence == 1 && serial_input == '[') {
					// We've hit the second character in an escape sequence
					characters_into_escape_sequence++;
					serial_input = -1; // Don't further process this character
				} else if(characters_into_escape_sequence == 2) {
					// Third (and last) character in the escape sequence
					escape_sequence_char = serial_input;
					serial_input = -1;  // Don't further process this character - we
										// deal with it as part of the escape sequence
					characters_into_escape_sequence = 0;
				} else {
					// Character was not part of an escape sequence (or we received
					// an invalid second character in the sequence). We'll process 
					// the data in the serial_input variable.
					characters_into_escape_sequence = 0;
				}
			}
		}
		
		// Process the input. 
		if((button==3 || escape_sequence_char=='D' || is_left()) && !paused) {
			// Attempt to move left
			(void)attempt_move(MOVE_LEFT);
		} else if((button==0 || escape_sequence_char=='C' || is_right()) && !paused) {
			// Attempt to move right
			(void)attempt_move(MOVE_RIGHT);
		} else if ((button==2 || escape_sequence_char == 'A' || is_up()) && !paused) {
			// Attempt to rotate
			(void)attempt_rotation();
		} else if ((escape_sequence_char == 'B' || is_down()) && !paused)  {
			// Attempt to drop block
			if(!attempt_drop_block_one_row()) {
				// Drop failed - fix block to board and add new block
				if(!fix_block_to_board_and_add_new_block()) {
					break;	// GAME OVER
				}
			} 
			last_drop_time = get_clock_ticks();
		} else if ((button==1 || serial_input == ' ') && !paused) {
			// Attempt to drop block from height
			
			// Attempt until failure
			while(attempt_drop_block_one_row()) {}
			// Drop failed - fix block to board and add new block	
			if(!fix_block_to_board_and_add_new_block()) {
				break;	// GAME OVER
			}
			
		} else if(serial_input == 'p' || serial_input == 'P') {
			// Unimplemented feature - pause/unpause the game until 'p' or 'P' is
			// pressed again. All other input (buttons, serial etc.) must be ignored.
			if(!paused) { // if running
				paused = 1; // pause game
				paused_time = get_clock_ticks(); // log the time it was paused
			}
			else { // if paused
				paused = 0; // unpause game
				last_drop_time += get_clock_ticks() - paused_time;
				// Shift the last_drop_time forward for the duration it was paused
			}
		} 
		// else - invalid input or we're part way through an escape sequence -
		// do nothing
		
		// Check for timer related events here
		if((get_clock_ticks() >= last_drop_time + (600 - (get_cleared_rows() * 30))) && !paused) {
			// 600ms (0.6 second) has passed since the last time we dropped
			// a block, so drop it now.
			if(!attempt_drop_block_one_row()) {
				// Drop failed - fix block to board and add new block
				if(!fix_block_to_board_and_add_new_block()) {
					break;	// GAME OVER
				}
			}
			last_drop_time = get_clock_ticks();
		}
	}
	// If we get here the game is over. 
}

void handle_game_over() {
	move_cursor(10,14);
	// Print a message to the terminal. 
	printf_P(PSTR("GAME OVER"));
	move_cursor(10,15);
	printf_P(PSTR("Press a button to start again"));
	while(button_pushed() == -1) {
		; // wait until a button has been pushed
	}
	
}

volatile uint8_t seven_seg_cc = 0;
uint8_t seven_seg_digits[10] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111};

void update_seven_segment(void) {
	
	seven_seg_cc = 1 ^ seven_seg_cc;
	
	if(seven_seg_cc == 0) {
		PORTC = seven_seg_digits[get_cleared_rows() % 10];
	} else {
		PORTC = seven_seg_digits[(get_cleared_rows() / 10) % 10] | 0x80;
	}
}

uint32_t joystick_time = 0;
uint8_t target_pin = 0; // the pin we're converting
uint16_t stick_x = 500; // Pin0
uint16_t stick_y = 500; // Pin1

void convert_joystick(void) {
	
	if (target_pin == 0) {
		ADMUX = (1 << REFS0) | (0 << MUX0);
	} 
	else {
		ADMUX = (1 << REFS0) | (1 << MUX0);
	}
	
	ADCSRA |= (1<<ADSC);
	
	while (ADCSRA & (1 < ADSC)) {
		; // repeat until converted
	}
	
	if (target_pin == 0) {
		stick_x = ADC;
	}
	else {
		stick_y = ADC;
	}

	target_pin = !target_pin; // do the other pin next time
}

uint8_t is_left(void) { // time is at least 150 ticks later than the last joystick input
	if ((stick_x > 850) && (get_clock_ticks() > joystick_time + 150)) {
		joystick_time = get_clock_ticks(); 
		return 1;
	}
	else {
		return 0;
	}
}

uint8_t is_right(void) {
	if ((stick_x < 150) && (get_clock_ticks() > joystick_time + 150)) {
		joystick_time = get_clock_ticks();
		return 1;
	}
	else {
		return 0;
	}
}

uint8_t is_up(void) { 
	if ((stick_y > 850) && (get_clock_ticks() > joystick_time + 300)) {
		joystick_time = get_clock_ticks(); 
		return 1;
	} 
	else {
		return 0;
	}
}

uint8_t is_down(void) { 
	if ((stick_y < 150) && (get_clock_ticks() > joystick_time + 150)) {
		joystick_time = get_clock_ticks(); 
		return 1;
	} 
	else {
		return 0;
	}
}
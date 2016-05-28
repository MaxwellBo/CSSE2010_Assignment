/*
 * score.c
 *
 * Written by Peter Sutton
 */

#include "score.h"

// Variable to keep track of the score. We declare it as static
// to ensure that it is only visible within this module - other
// modules should call the functions below to modify/access the
// variable.
static uint32_t score;
static uint8_t cleared_rows;

void init_score(void) {
	score = 0;
}

void add_to_score(uint16_t value) {
	score += value;
}

uint32_t get_score(void) {
	return score;
}

void init_cleared_rows(void) {
	cleared_rows = 0;
}

void increment_cleared_rows(void) {
	if(cleared_rows < 99) {
		cleared_rows++;
	}
}

uint8_t get_cleared_rows(void) {
	return cleared_rows;
}
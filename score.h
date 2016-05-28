/*
 * score.h
 * 
 * Author: Peter Sutton
 */

#ifndef SCORE_H_
#define SCORE_H_

#include <stdint.h>

void init_score(void);
void add_to_score(uint16_t value);
uint32_t get_score(void);
void init_cleared_rows(void);
void increment_cleared_rows(void);
uint8_t get_cleared_rows(void);

#endif /* SCORE_H_ */
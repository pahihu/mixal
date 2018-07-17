/* MIX simulator, copyright 1994 by Darius Bacon */ 
#ifndef RUN_H
#define RUN_H

#include "mix.h"

void set_initial_state(void);
void set_trace_count(unsigned value);
void print_CPU_state(void);
void print_DUMP(void);
void run(void);
Cell memory_fetch(Address address);                 /* fetch memory from address, range checked */
void memory_store(Address address, Cell cell);      /* store cell at address, range checked */
typedef enum{normal_state, control_state} State;
State get_internal_state(void);			    /* internal machine state */

#endif

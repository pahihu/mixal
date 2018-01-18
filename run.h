/* MIX simulator, copyright 1994 by Darius Bacon */ 
#ifndef RUN_H
#define RUN_H

#include "mix.h"

extern Cell memory[];

void set_initial_state(void);
void print_CPU_state(void);
void run(void);
Cell memory_fetch(Address address);                 /* fetch memory from address, range checked */
void memory_store(Address address, Cell cell);      /* store cell at address, range checked */

#endif

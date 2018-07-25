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
void set_wait_state(void);			    /* set wait state */

#define MIXCONFIG_BINARY	(1U)		    /* binary MIX */
#define MIXCONFIG_INTERRUPT	(2U)		    /* interrupt facility */
#define MIXCONFIG_FLOAT		(4U)		    /* FP attachment */
#define MIXCONFIG_CORE		(8U)		    /* core memory */
#define MIXCONFIG_MASTER	(128U)		    /* Mixmaster */
void set_configuration(unsigned flags);		    /* set MIX configuration */

#endif

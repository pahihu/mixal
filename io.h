/* MIX simulator, copyright 1994 by Darius Bacon */ 
#ifndef IO_H
#define IO_H

#include "mix.h"

extern unsigned long idle_time;             /* in Tyme units, waiting for I/O */

void io_init(void);
unsigned io_control(Byte device, Cell argument, Cell offset);   /* returns idle clocks */
unsigned do_input(Byte device, Cell argument, Address buffer);
unsigned do_output(Byte device, Cell argument, Address buffer);
void io_redirect(void);                     /* redirect reader/punch/printer to files */
void io_set_go_device(Byte device);         /* set GO device */
Byte io_go_device(void);                    /* return GO device number */
void do_scheduled_io(unsigned clocks);      /* simulate I/O devices do clocks steps */
Flag io_device_busy(Byte device);           /* true if device busy */
Flag io_scheduled(void);                    /* true if has scheduled I/O in the future */
typedef enum {read_access, write_access} Access;
Flag io_incomplete(Address address, Access access); /* true if incomplete I/O operation affects the address */
unsigned io_finish(void);                   /* wait for outstanding I/O to complete, return time spent */

Flag io_has_interrupt_facility(void);	    /* true if interrupt facility present */
void io_set_interrupt_facility(void);	    /* interrupt facility is present */
Flag io_pending_interrupt(Byte *device);    /* true if interrupt pending, device is the unit number */

#endif

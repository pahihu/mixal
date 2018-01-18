/* MIX simulator, copyright 1994 by Darius Bacon */ 
#ifndef IO_H
#define IO_H

#include "mix.h"

void io_init(void);
void io_control(Byte device, Cell argument, Cell offset);
void do_input(Byte device, Cell argument, Address buffer);
void do_output(Byte device, Cell argument, Address buffer);
void io_redirect(void);                     /* redirect reader/punch/printer to files */
void io_set_go_device(Byte device);         /* set GO device */
Byte io_go_device(void);                    /* return GO device number */
void do_scheduled_io(unsigned tyme);        /* simulate I/O devices for tyme clocks */
Flag io_device_busy(Byte device);           /* true if device busy */
Flag io_scheduled(void);                    /* true if has scheduled I/O in the future */
typedef enum {read_access, write_access} Access;
Flag io_incomplete(Address address, Access access); /* true if incomplete I/O operation affects the address */

#endif

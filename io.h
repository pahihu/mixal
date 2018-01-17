/* MIX simulator, copyright 1994 by Darius Bacon */ 
#ifndef IO_H
#define IO_H

#include "mix.h"

void io_init(void);
void io_control(Byte device, Cell argument, Cell offset);
void do_input(Byte device, Cell argument, Address buffer);
void do_output(Byte device, Cell argument, Address buffer);
void io_redirect(void);
void io_set_go_device(Byte device);
Byte io_go_device(void);
void do_scheduled_io(unsigned tyme);
Flag io_device_busy(Byte device);
Flag io_scheduled(void);

#endif

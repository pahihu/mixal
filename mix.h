/* MIX simulator, copyright 1994 by Darius Bacon */ 
#ifndef MIX_H
#define MIX_H

#include <assert.h>
#include <stdarg.h>

/* Some standard stuff all or most modules have in common. */

typedef enum { false, true } Flag;

/* Dump helpful debugging info if true: */
#define VERBOSE false

#define not_reached false
#define NOT_REACHED     assert(not_reached)

void install_error_handler(void (*handler)(const char *, va_list));
void warn(const char *message, ...);
void error(const char *message, ...);
void fatal_error(const char *message, ...);

#define max_identifier_length 32
#define memory_size 4000

/* Device types: */
enum DeviceType { drum, disk, tape, card_in, card_out, printer, console };
extern enum DeviceType current_device_type;
#define LAST_DEVICE_TYPE (console)

#include "cell.h"

#endif

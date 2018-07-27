/* MIX simulator, copyright 1994 by Darius Bacon */ 
#ifndef IO_H
#define IO_H

#include "mix.h"


/* --- Device numbers --- */
#define DEV_MT0   ((Byte) 000)
#define DEV_MT1   ((Byte) 001)
#define DEV_MT2   ((Byte) 002)
#define DEV_MT3   ((Byte) 003)
#define DEV_MT4   ((Byte) 004)
#define DEV_MT5   ((Byte) 005)
#define DEV_MT6   ((Byte) 006)
#define DEV_MT7   ((Byte) 007)
#define DEV_DK0   ((Byte) 010)
#define DEV_DK1   ((Byte) 011)
#define DEV_DK2   ((Byte) 012)
#define DEV_DK3   ((Byte) 013)
#define DEV_DK4   ((Byte) 014)
#define DEV_DK5   ((Byte) 015)
#define DEV_DR0   ((Byte) 016)
#define DEV_DR1   ((Byte) 017)
#define DEV_CR0   ((Byte) 020)
#define DEV_CP0   ((Byte) 021)
#define DEV_LP0   ((Byte) 022)
#define DEV_TT0   ((Byte) 023)
#define DEV_XXX	  ((Byte) 255)

extern unsigned long idle_time;             /* in Tyme units, waiting for I/O */

void io_init(void);
void io_shutdown(void);

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
unsigned io_complete(void);                 /* wait for outstanding I/O to complete, return time spent */

Flag io_pending_interrupt(Byte *device);    /* true if interrupt pending, device is the unit number */

void punch_object_deck(const char *title, Address start_address);

#endif

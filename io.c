/* MIX simulator, copyright 1994 by Darius Bacon */ 
#include "mix.h"
#include "charset.h"
#include "io.h"
#include "run.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int redirect_devices = 0;        /* stdin/stdout is card reader/printer*/
static Byte go_device = 255;            /* no default GO device */
static unsigned next_scheduled_io = 0;  /* next I/O time */

/* --- Device tables --- */

enum DeviceType current_device_type;
typedef enum {control, input, output} Operation;

/* The device table: */
static struct {
    const enum DeviceType type;
    FILE *file;
    long position;		/* used by random-access devices */
  /*    const char *filename; */
    unsigned busy;              /* expiration time */
    Operation operation;
    Cell argument;
    Cell offset;
    Address buffer; 
} devices[] = {
    {tape}, {tape}, {tape}, {tape}, {tape}, {tape}, {tape}, {tape}, 
    {disk}, {disk}, {disk}, {disk}, {disk}, {disk}, {drum}, {drum}, 
    {card_in},
    {card_out},
    {printer},
    {console}
};

#define num_devices ( sizeof devices / sizeof devices[0] )

/* add an assign_file(device, filename) operation, too */
/* and unassign? */

typedef void IOHandler(unsigned, Cell, Address);
typedef void IOCHandler(unsigned, Cell, Cell);

static IOHandler tape_in, disk_in, text_in, console_in, no_in;
static IOHandler tape_out, disk_out, text_out, console_out, no_out;
static IOCHandler tape_ioc, disk_ioc, no_ioc, printer_ioc;

/* The device-class table: */
/*** need to distinguish read/write permission... */
static const struct Device_attributes {
    const char *base_filename;
    unsigned block_size;
    IOHandler *in_handler;
    IOHandler *out_handler;
    IOCHandler *ioc_handler;
    unsigned  io_time;
    unsigned  seek_time;
} methods[] = {

/* tape */      { "tape",   100, tape_in, tape_out, tape_ioc,       50,    50},
/* disk */      { "disk",   100, disk_in, disk_out, disk_ioc,      250,     1},
/* drum */      { "drum",   100, disk_in, disk_out, disk_ioc,      125,     0},
/* card_in */   { "reader",  16, text_in, no_out,   no_ioc,      10000,     0},
/* card_out */  { "punch",   16, no_in,   text_out, no_ioc,      20000,     0},
/* printer */   { "printer", 24, no_in,   text_out, printer_ioc,  7500, 10000},
/* console */   { NULL,      14, console_in, console_out, no_ioc,    0,     0}

};

static const struct Device_attributes *attributes (unsigned device)
{
    return &methods[devices[device].type];
}

static unsigned block_size(unsigned device)
{
    return attributes(device)->block_size;
}

static unsigned external_block_size(unsigned device)
{
    return 6 * block_size(device) + 1;
}

static FILE *assigned_file(unsigned device)
{
    if (redirect_devices)
        return devices[device].file;

    /* glibc2.1 fix -ajk */
    switch (devices[device].type) {
    case card_in:
        return stdin; break;
    case card_out: case printer:
        return stdout; break;
    default:
        return devices[device].file;
    }
}

static char *device_filename(Byte device)
{
    static char filename[FILENAME_MAX];
    if (device < 16)
        sprintf(filename, "%s%02d",
	        attributes(device)->base_filename, device);
    else
        strcpy(filename, attributes(device)->base_filename);
    return filename;
}

static void ensure_open(Byte device)
{
    if (num_devices <= device)
        error("Unknown device - %02o", device);
    if (!assigned_file(device)){
	if (attributes(device)->base_filename) {
	    const char *filename = device_filename(device);
	    if (!(devices[device].file = fopen(filename, "r+b"))
		&& !(devices[device].file = fopen(filename, "w+b")))
	        error("%s: %s", filename, strerror(errno));
	    devices[device].position = 0;
	} else
	    error("No file assigned to device %02o (type %d)", device, devices[device].type);
    }
}

static void io_schedule(Byte device, Operation operation, 
                        Cell argument, Cell offset, Address buffer)
{
    unsigned busy, seek_time;
    enum DeviceType device_type;

    devices[device].operation = operation;
    devices[device].argument  = argument;
    devices[device].offset    = offset;
    devices[device].buffer    = buffer;

    device_type = devices[device].type;
    seek_time   = attributes(device)->seek_time;

    busy = 0;
    if (operation == control) {
        if (device_type == tape || device_type == disk)
            busy = seek_time * labs(devices[device].position - offset);
        else if (device_type == printer)
            busy = seek_time;
    }
    else
        busy  = attributes(device)->io_time;

    if ((operation == input) || (operation == output)) {
        if (device_type == disk)
            busy += seek_time * labs(devices[device].position - offset);
    }
    devices[device].busy = busy;

    if (next_scheduled_io) {
        if (busy < next_scheduled_io)
            next_scheduled_io = busy;
    } else
        next_scheduled_io = busy;
}

static void do_io(Byte device)
{
    Cell argument, offset;
    Address buffer;

    argument = devices[device].argument;
    offset   = devices[device].offset;
    buffer   = devices[device].buffer;

    ensure_open(device);
    current_device_type = devices[device].type;
    switch (devices[device].operation) {
    case control:
        attributes(device)->ioc_handler(device, argument, offset); break;
    case input:
        attributes(device)->in_handler(device, argument, buffer); break;
    case output:
        attributes(device)->out_handler(device, argument, buffer); break;
    }
    devices[device].busy = 0;
}

void io_control(Byte device, Cell argument, Cell offset)
{
    if (devices[device].busy)
        do_scheduled_io(devices[device].busy);

    if (attributes(device)->io_time)
        io_schedule(device, control, argument, offset, 0); 
    else {
        ensure_open(device);
        attributes(device)->ioc_handler(device, argument, offset);
    }
}

void do_input(Byte device, Cell argument, Address buffer)
{
    if (devices[device].busy)
        do_scheduled_io(devices[device].busy);

    if (attributes(device)->io_time)
        io_schedule(device, input, argument, 0, buffer);
    else {
        ensure_open(device);
        attributes(device)->in_handler(device, argument, buffer);
    }
}

void do_output(Byte device, Cell argument, Address buffer)
{
    if (devices[device].busy)
        do_scheduled_io(devices[device].busy);

    if (attributes(device)->io_time)
        io_schedule(device, output, argument, 0, buffer);
    else {
        ensure_open(device);
        attributes(device)->out_handler(device, argument, buffer);
    }
}

/* --- Unsupported input or output --- */

static void no_ioc(unsigned device, Cell argument, Cell offset)
{
    error("IOC undefined for device %02o", device);
}

static void no_in(unsigned device, Cell argument, Address buffer)
{
    error("Input not allowed for device %02o", device);
}

static void no_out(unsigned device, Cell argument, Address buffer)
{
    error("Output not allowed for device %02o", device);
}

/* --- Text devices --- */

/* Read a line from -file- into memory[buffer..buffer+size). 
   (Big-endian byte order, padded with 0 bytes if the line is less 
   than -size- cells long.  The signs of the cells are set to '+'. 
   If the line is longer than 5*size bytes, only the first 5*size  
   bytes get read. */
static void read_line(FILE* file, Address buffer, unsigned size)
{
    unsigned i, b;
    Flag past_end = false;
    for (i = 0; i < size; ++i) {
	    Cell cell = zero;
	    if (memory_size <= buffer + i)
        /*** I think we need memory_fetch() and memory_store() functions... */
	        error("Address out of range");
	    for (b = 1; b <= 5; ++b) {
	        Byte mix_char;
	        if (past_end)
		        mix_char = (Byte) 0;
	        else {
		        int c = fgetc(file);
		        if (c == '\n' || c == EOF)
		            past_end = true, mix_char = (Byte) 0;
		        else
		            mix_char = C_char_to_mix((char) c);
	        }
	        cell = set_byte(mix_char, b, cell);
	    }
	    memory[buffer + i] = cell;
    }
}

static void write_cell(Cell cell, FILE *outfile, Flag text)
{
    unsigned i;
    if (!text)
        fputc(is_negative(cell) ? '-' : ' ', outfile);
    for (i = 1; i <= 5; ++i)
        fputc(mix_to_C_char(get_byte(i, cell)), outfile);
}

static void write_line(FILE *file, Address buffer, unsigned size, Flag text)
{
    unsigned i;
    for (i = 0; i < size; ++i) {
	if (memory_size <= buffer + i)
	    error("Address out of range");
	write_cell(memory[buffer + i], file, text);
    }
    fputc('\n', file);
}

static void printer_ioc(Byte device, Cell argument, Cell offset)
{
    if (magnitude(offset) != 0)
        error("IOC argument undefined for printer device %02o", device);
    fputc('\f', assigned_file(device));
}

/* 11-punch: ~JKLMNOPQR instead of 0123456789 */
static int overpunched(Cell cell)
{
    Byte mix_char = get_byte(5, cell);
    return (9 < mix_char) && (mix_char < 20);
}

static Cell flip_chars(Cell cell)
{
    int i;
    Cell ret = zero;

    for (i = 1; i <= 5; i++) {
        Byte mix_char = get_byte(i, cell);
        if ((29 < mix_char) && (mix_char < 40))
            mix_char = mix_char - 20;
	    ret = set_byte(mix_char, i, ret);
    }
    return ret;
}

static void text_in(Byte device, Cell argument, Address buffer)
{
    int i;

    read_line(assigned_file(device), buffer, block_size(device));
    if (current_device_type == card_in) {
        for (i = 0; i < 16; i += 2) {
            Cell cell1 = memory[buffer + i    ];
            Cell cell2 = memory[buffer + i + 1];
            if (overpunched(cell2)) {
                memory[buffer + i    ] = flip_chars(cell1);
                memory[buffer + i + 1] = flip_chars(cell2);
            }
        }
    }
}

static void text_out(Byte device, Cell argument, Address buffer)
{
    write_line(assigned_file(device), buffer, block_size(device), true);
}

/* --- Block devices --- */
/*** Make this section more robust. */
/*** And either these errors should be fatal errors or we need to think
     about recovery. */

static void set_file_position(Byte device, unsigned block, Flag writing)
{
	if (devices[device].position != (long) block) {
		if (fseek(assigned_file(device),
	    		(long) block * external_block_size(device),
	    		SEEK_SET))
      		error("Device %02o: %s", device, strerror(errno));
  		devices[device].position = (long) block;
	}
}

static unsigned get_file_position(Byte device)
{
	return (unsigned) ftell(assigned_file(device)) 
			/ external_block_size(device);
}

/* Read a block from -device- into memory[buffer..buffer+block_size(device)). 
   (Big-endian byte order, with words represented by 6 native C characters: 
   the sign ('-' or ' '), followed by 5 characters whose MIX equivalents 
   code for the corresponding bytes.)  The block should end with a '\n'. */
static void read_block(Byte device, Address buffer)
{
    FILE *file = assigned_file(device);
    unsigned size = block_size(device);
    unsigned i, b;
    for (i = 0; i < size; ++i) {
	int c;
	Cell cell = zero;
	if (memory_size <= buffer + i)
	    error("Address out of range -- read_block");

	c = fgetc(file);
	if (c == EOF)
	    error("Unexpected EOF reading from device %02o", device);
	else if (c == '-')
	    cell = negative(cell);

	for (b = 1; b <= 5; ++b) {
	    c = fgetc(file);
	    if (c == EOF)
		error("Unexpected EOF reading from device %02o", device);
	    cell = set_byte(C_char_to_mix((char) c), b, cell);
	}
	memory[buffer + i] = cell;
    }
    fgetc(file);			/* should be '\n' */
}

/* The inverse of read_block. */
static void write_block(Byte device, Address buffer)
{
    write_line(assigned_file(device), buffer, block_size(device), false);
}

/* --- Tapes --- */

static void tape_ioc(unsigned device, Cell argument, Cell offset)
{
	unsigned block_num, position;
	FILE *fd;
	int c;

	block_num = (unsigned) magnitude(offset);
	if (block_num == 0)
		set_file_position(device, 0, false);
	else if (is_negative(offset)) {
                position = get_file_position(device);
                if (position < block_num)
                        block_num = 0;
                else
                        block_num = position - block_num;
	        set_file_position(device, block_num, false);
	} else {
		fd = assigned_file(device);
		position = get_file_position(device);
		while (block_num--) {
			set_file_position(device, ++position, false);
			c = fgetc(fd);
			if (c == EOF)
				break;
			if (ungetc(c, fd) == EOF)
      			error("Device %02o: %s", device, strerror(errno));
		}
	}
}

static void tape_in(unsigned device, Cell argument, Address buffer)
{
    read_block(device, buffer);
}

static void tape_out(unsigned device, Cell argument, Address buffer)
{
	write_block(device, buffer);
}

/* --- Disks --- */

static void disk_ioc(Byte device, Cell argument, Cell offset)
{
	unsigned block_num;

    if (magnitude(offset) != 0)
        error("IOC argument undefined for disk device %02o", device);

	block_num = (unsigned) field(make_field_spec(4, 5), argument);
	set_file_position(device, block_num, false);
}

static void disk_in(Byte device, Cell argument, Address buffer)
{
    unsigned block_num = (unsigned) field(make_field_spec(4, 5), argument);
    set_file_position(device, block_num, false);
    read_block(device, buffer);
}

static void disk_out(Byte device, Cell argument, Address buffer)
{
    unsigned block_num = (unsigned) field(make_field_spec(4, 5), argument);
    set_file_position(device, block_num, true);
    write_block(device, buffer);
}

/* --- The console (typewriter/paper tape) --- */
/* Always connected to stdin/stdout, for simplicity. */

static void console_in(Byte device, Cell argument, Address buffer)
{
    read_line(stdin, buffer, block_size(device));
}

static void console_out(Byte device, Cell argument, Address buffer)
{
    write_line(stdout, buffer, block_size(device), true);
}

void io_redirect(void)
{
   redirect_devices = 1;
}

void io_set_go_device(Byte device)
{
    if (num_devices <= device)
        error("Unknown device - %02o", device);

    go_device = device;
}

Byte io_go_device(void)
{
    return go_device;
}

void do_scheduled_io(unsigned tyme)
{
    int i;
    Byte device;

    for (i = 0; i < tyme; i++) {
        next_scheduled_io = (unsigned) -1;
        for (device = 0; device < num_devices; device++) {
            if (devices[device].busy) {
                devices[device].busy--;
                if (0 == devices[device].busy)
                    do_io(device);
                else if (devices[device].busy < next_scheduled_io)
                    next_scheduled_io = devices[device].busy;
            }
        }
        if (next_scheduled_io == (unsigned) -1)
            next_scheduled_io = 0;
    }

}

Flag io_device_busy(Byte device)
{
    return devices[device].busy ? true : false;
}

Flag io_scheduled(void)
{
    return next_scheduled_io ? true : false;
}

void io_init(void)
{
    Byte device;

    for (device = 0; device < num_devices; device++) {
        devices[device].busy = 0;
    }
}
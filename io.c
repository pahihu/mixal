/* MIX simulator, copyright 1994 by Darius Bacon */ 
#include "mix.h"
#include "charset.h"
#include "io.h"
#include "run.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int dbg = 0;                         /* debug flag */

static int redirect_devices = 0;            /* stdin/stdout is card reader/printer*/
static Byte go_device = DEV_CR0;            /* default GO device is the card reader */
static unsigned next_scheduled_io = 0;      /* next I/O time */
unsigned long idle_time = 0;                /* waiting for I/O */
int *incomplete;                            /* mark cell used by I/O */
int *control_incomplete;                    /* mark cell used by I/O */
#define READ    (-1)
#define WRITE   (-((int) num_devices + 1))

/* --- Device tables --- */

enum DeviceType current_device_type;
typedef enum {control, input, output} Operation;
static char* operations[] = {"control", "read", "write"};

#ifdef WIN32
#define	TXT_APPEND   "at"
#define	TXT_RDONLY   "rt"
#else
#define	TXT_APPEND   "at"
#define	TXT_RDONLY   "rt"
#endif
#define	BIN_RWRITE   "r+b"
#define	BIN_CREATE   "w+b"

/* The device table: */
static struct {
    const enum DeviceType type;
    FILE *file;
    long position;           /* used by random-access devices */
    unsigned busy;           /* expiration time */
    Address pc;              /* I/O instruction address */
    Operation operation;     /* control, input, output */
    Cell rX;                 /* contents of r[X] */
    Cell offset;             /* M */
    Address buffer;          /* source/destination buffer address */
    Flag int_request;        /* request interrupt on completion */
    Flag int_pending;        /* interrupt pending */
    Flag rewound;            /* tape rewound */
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

static void do_io(Byte device);

/* The device-class table: */
/*** need to distinguish read/write permission... */
static const struct Device_attributes {
    const char *base_filename;
    const char *fam;
    unsigned  size;
    unsigned block_size;
    IOHandler *in_handler;
    IOHandler *out_handler;
    IOCHandler *ioc_handler;
    unsigned  io_time;
    unsigned  seek_time;
} methods[] = {

/* Device characteristics
 * ======================
 * drum B430
 *  3750rpm (?)
 *  2ms transfer time + max. 16ms rotational delay
 *
 * disk B475
 *  1500rpm, head/track, 100Kcps
 *  5ms transfer time + max. 40ms rotational delay
 *
 * tape B421
 *  90ips, 200/556cpi, 2400' reel, 18KC/50KC, 320ips high-speed rewind
 *  417c gap - 8.3ms
 *  100W requires 500 BCD chars = 0.9" + 0.75" gap = 1.65" @ 556cpi
 *  10' load-point marker ... 14' end-of-reel marker => 28,512" usable
 *                                           17280 100W records
 *  4.2ms start time + 10ms transfer time + 4.1ms stop time
 *       4.2ms  700 Ty
 *       4.1ms  683 Ty
 *  assume
 *      92.0ms  start time @ load-point    15333 Ty
 *
 * card_in B122
 *  200cpm, 80 column, 450 card hopper
 *
 * card_out B303
 *  100cpm, 80 column, 800 card stacker
 *
 * printer B320
 *  475lpm
 *
 * console ASR33
 *  10cps
 *
 */
{ "drum",   BIN_RWRITE,  512, 100, disk_in, disk_out, disk_ioc,    333, 2667},
{ "disk",   BIN_RWRITE, 4096, 100, disk_in, disk_out, disk_ioc,    833, 6667},
{ "tape",   BIN_RWRITE,17280, 100, tape_in, tape_out, tape_ioc,   3050,  859},
{ "reader", TXT_RDONLY,    0,  16, text_in,   no_out, no_ioc,    50000,    0},
{ "punch",  TXT_APPEND,    0,  16, no_in,   text_out, no_ioc,   100000,    0},
{ "printer",TXT_APPEND,    0,  24, no_in,   text_out, printer_ioc,21053, 833},
{ NULL,           NULL,    0,  14, console_in,console_out, no_ioc,1166667, 0}

};

#define MT_START_LP     15333
#define MT_START          700
#define MT_TRANSFER      1667
#define MT_STOP           683

/* paper tape reader B141 1000/500cps, 10cpi */
/* paper tape punch B341 100cps, 10cpi */

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
    if (!assigned_file(device)) {
        if (attributes(device)->base_filename) {
            const char *filename = device_filename(device);
            const char *fam = attributes(device)->fam;
            if (!(devices[device].file = fopen(filename, fam))) {
                if (!strcmp(fam, BIN_RWRITE))
                    devices[device].file = fopen(filename, BIN_CREATE);
                if (!devices[device].file)
                    error("Cannot open device file %s: %s", filename, strerror(errno));
            }
            devices[device].position = 0;
        } else
            error("No file assigned to device %02o (type %d)", device, devices[device].type);
    }
}

static int fetch_incomplete(Address address)
{
    return address < 0 ? control_incomplete[-address] : incomplete[address];
}

static void store_incomplete(Address address, int value)
{
    if (address < 0)
        control_incomplete[-address] = value;
    else
        incomplete[address] = value;
}

static void io_mark_incomplete(Address buffer, unsigned block_size, int mark)
{
    int i, current_count;

    if (buffer < 0) {
        if (normal_state == get_internal_state())
            error("Cannot access control store in normal state");
    }

    if (dbg > 2) {
        fflush(stdout);
        fprintf(stderr, "io_mark_incomplete(%d,%d,%d)\n",
                buffer, block_size, mark);
    }
    for (i = 0; i < block_size; i++) {
        if (memory_size <= magnitude(buffer + i))
            error("Address out of range -- io_mark_incomplete");
        current_count = fetch_incomplete(buffer + i);
        if (current_count + mark < 0) {
            if (dbg > 3) {
                fflush(stdout);
                fprintf(stderr, "current: %d, mark: %d\n", current_count, mark);
            }
            error("Overlapping buffers -- io_mark_incomplete");
        }
        store_incomplete(buffer + i, current_count + mark);
    }
}

static void io_schedule(Address pc,
                        Byte device, Operation operation,
                        Cell rX, Cell offset, Address buffer)
{
    unsigned busy, seek_time, io_time, u;
    enum DeviceType device_type;
    long position;
    Flag rewound;

    if (dbg > 2) {
        fflush(stdout);
        fprintf(stderr,"io_schedule(%s, %d, %s, %s%lo, %ld, %d)\n",
                address_to_string(pc),
                device, operations[operation],
                is_negative(rX) ? "-" : "", magnitude(rX),
                offset, buffer);
    }

    rewound = devices[device].rewound;

    devices[device].pc        = pc;
    devices[device].operation = operation;
    devices[device].rX        = rX;
    devices[device].offset    = offset;
    devices[device].buffer    = buffer;

    position      = devices[device].position;
    device_type   = devices[device].type;
    seek_time     = attributes(device)->seek_time;
    io_time       = attributes(device)->io_time;

    busy = 0;
    if (operation == control) {
        switch (device_type) {
        case tape:
            u = magnitude(offset);
            if (u == 0) {                       /* --- rewind --- */
                devices[device].rewound = true;
                busy = position * seek_time;
            }
            else if (is_negative(offset)) {     /* --- skip backward --- */
                busy = u < position ? u : position;
                busy *= io_time;
            }
            else {                              /* --- skip forward --- */
                busy = position + u;
                if (busy >= attributes(device)->size)
                    busy = attributes(device)->size - position;
                busy *= io_time;
                if (busy != 0 && rewound == true) {
                    busy += MT_START_LP - MT_START;
                    devices[device].rewound = false;
                }
            }
            break;
        case disk:
            // head/track
            busy = 0;
            // 1 head/surface
            // busy = labs(position - magnitude(offset)) / 64;
            break;
        case printer:
            busy = (132 - (position % 132)) * seek_time;
            break;
        default:
            break;
        }
    }
    else {
        if ((device_type != console) || (operation != input)) {
            busy = io_time;
            if (device_type == tape && rewound == true) {
                busy += (MT_START_LP - MT_START);
                devices[device].rewound = false;
            }
        }
    }

    if (operation == input || operation == output) {
        /* rotational delay */
        if (device_type == disk || device_type == drum) {
            unsigned sector = 64 * (elapsed_time % seek_time) / (double)seek_time;
            u = magnitude(offset) % 64;
            if (u >= sector)
                offset -= sector;
            else
                u += 64 - sector;
            busy += seek_time * u / 64.0;
        }
        io_mark_incomplete(buffer,
                           attributes(device)->block_size,
                           operation == input ? WRITE : READ);
    }
    if (dbg > 2) {
        fflush(stdout);
        fprintf(stderr,"io_schedule: busy = %d next_scheduled_io = %u\n", busy, next_scheduled_io);
    }
    devices[device].busy = busy;

    if (0 == busy) {
        do_io (device);
        return;
    }

    if (next_scheduled_io) {
        if (busy < next_scheduled_io)
            next_scheduled_io = busy;
    } else
        next_scheduled_io = busy;
}

static void do_io(Byte device)
{
    Cell rX, offset;
    Address buffer;
    Operation operation;

    operation = devices[device].operation;
    rX        = devices[device].rX;
    offset    = devices[device].offset;
    buffer    = devices[device].buffer;

    if (dbg > 1) {
        fflush(stdout);
        fprintf(stderr,"do_io(%s, %d, %s, %s%lo, %ld, %d, @%ld)\n",
                address_to_string(devices[device].pc),
                device, operations[operation],
                is_negative(rX) ? "-" : "", magnitude(rX),
                offset, buffer,
                devices[device].position);
    }

    current_device_type = devices[device].type;
    switch (operation) {
    case control:
        attributes(device)->ioc_handler(device, rX, offset); break;
    case input:
        attributes(device)->in_handler(device, rX, buffer); break;
    case output:
        attributes(device)->out_handler(device, rX, buffer);
        if (current_device_type == printer)
            devices[device].position++;
    }
    
    if (operation != control)
        io_mark_incomplete(buffer,
                           attributes(device)->block_size, 
                           operation == input ? -WRITE : -READ);
        
    if (devices[device].int_request == true)
        devices[device].int_pending = true;

    devices[device].busy = 0;
}

static unsigned do_operation(Address pc,
                             Byte device, Operation operation,
                             Cell rX, Cell offset, Address buffer)
{
    unsigned clocks = 0;

    /*
    if (true == devices[device].int_pending)
        error("Interrupt pending - %02o", device);
    */

    ensure_open(device);
    if (devices[device].busy) {
        clocks = 1;
        idle_time += clocks;
        do_scheduled_io(clocks);
        set_wait_state();
        return clocks;
    }

    if (control_state == get_internal_state())
        devices[device].int_request = true;

    if (attributes(device)->io_time)
        io_schedule(pc, device, operation, rX, offset, buffer);
    else {
        switch (operation) {
        case control: attributes(device)->ioc_handler(device, rX, offset); break;
        case input:   attributes(device)->in_handler(device, rX, buffer); break;
        case output:  attributes(device)->out_handler(device, rX, buffer); break;
        }
        if (devices[device].int_request == true)
            devices[device].int_pending = true;
    }
    return clocks;
}

unsigned io_control(Address pc, Byte device, Cell rX, Cell offset)
{
    return do_operation(pc, device, control, rX, offset, 0);
}

unsigned do_input(Address pc, Byte device, Cell rX, Address buffer)
{
    return do_operation(pc, device, input, rX, 0, buffer);
}

unsigned  do_output(Address pc, Byte device, Cell rX, Address buffer)
{
    return do_operation(pc, device, output, rX, 0, buffer);
}

/* --- Unsupported input or output --- */

static void no_ioc(unsigned device, Cell rX, Cell offset)
{
    error("IOC undefined for device %02o", device);
}

static void no_in(unsigned device, Cell rX, Address buffer)
{
    error("Input not allowed for device %02o", device);
}

static void no_out(unsigned device, Cell rX, Address buffer)
{
    error("Output not allowed for device %02o", device);
}


static void write_cell(Cell cell, FILE *outfile, Flag text);

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
        for (b = 1; b <= 5; ++b) {
            Byte mix_char;
            if (past_end)
                mix_char = (Byte) 0;
            else {
                int c = fgetc(file);
                if (c == '\n' || c == EOF) {
                    past_end = true, mix_char = (Byte) 0;
                } else
                    mix_char = C_char_to_mix((char) c);
            }
            cell = set_byte(mix_char, b, cell);
        }
        if (dbg) {
            printf("read_line: %3d = ", buffer + i);
            write_cell (cell, stdout, true);
            printf("\n");
        }
        memory_store(buffer + i, cell);
    }
    if (false == past_end) {
        int c = fgetc(file);
        if (c != '\n' && c != EOF)
            error("Line not terminated (%d)", c);
    }
}

static void write_cell(Cell cell, FILE *outfile, Flag text)
{
    unsigned i;

    if (text) {
        for (i = 1; i <= 5; ++i)
            fputc(mix_to_C_char(get_byte(i, cell)), outfile);
        return;
    }

    fputc(is_negative(cell) ? '-' : ' ', outfile);
    for (i = 1; i <= 5; ++i)
        fputc(get_byte(i, cell), outfile);
}

static void write_line(FILE *file, Address buffer, unsigned size, Flag text)
{
    unsigned i;
    for (i = 0; i < size; ++i)
        write_cell(memory_fetch(buffer + i), file, text);
    fputc('\n', file);
}

static void printer_ioc(Byte device, Cell rX, Cell offset)
{
    if (magnitude(offset) != 0)
        error("IOC argument undefined for printer device %02o", device);
    fputc('\f', assigned_file(device));
}

static void text_in(Byte device, Cell rX, Address buffer)
{
    read_line(assigned_file(device), buffer, block_size(device));
}

static void text_out(Byte device, Cell rX, Address buffer)
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
        if (attributes(device)->size != 0 && attributes(device)->size <= block)
            error("Device %02o full!");
        else {
            if (fseek(assigned_file(device),
                      (long) block * external_block_size(device),
                  SEEK_SET))
                error("Device %02o: %s", device, strerror(errno));
              devices[device].position = (long) block;
        }
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
    int c;

    for (i = 0; i < size; ++i) {
        Cell cell = zero;
        c = fgetc(file);
        if (c == EOF) {
            if (true /*get_halting()*/) {
                fflush(stdout);
                fprintf(stderr, "Unexpected EOF reading from device %02o\n", device);
                return;
            }
            else
                error("Unexpected EOF reading from device %02o", device);
        }
        else if (c == '-')
            cell = negative(cell);
    
        for (b = 1; b <= 5; ++b) {
            c = fgetc(file);
            if (c == EOF) {
                if (true /*get_halting()*/) {
                    fflush(stdout);
                    fprintf(stderr, "Unexpected EOF reading from device %02o\n", device);
                    return;
                }
                else
                    error("Unexpected EOF reading from device %02o", device);
            }
            cell = set_byte(C_int_to_mix(c), b, cell);
        }
        memory_store(buffer + i, cell);
    }
    c = fgetc(file);            /* should be '\n' */
    assert(c == '\n');
}

/* The inverse of read_block. */
static void write_block(Byte device, Address buffer)
{
    write_line(assigned_file(device), buffer, block_size(device), false);
}

/* --- Tapes --- */

static void tape_ioc(unsigned device, Cell rX, Cell offset)
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
    devices[device].position = get_file_position(device);
}

static void tape_in(unsigned device, Cell rX, Address buffer)
{
    read_block(device, buffer);
    devices[device].position = get_file_position(device);
}

static void tape_out(unsigned device, Cell rX, Address buffer)
{
    write_block(device, buffer);
    devices[device].position = get_file_position(device);
}

/* --- Disks --- */

static void disk_ioc(Byte device, Cell rX, Cell offset)
{
    unsigned block_num;

    if (magnitude(offset) != 0)
        error("IOC argument undefined for disk device %02o", device);

    block_num = (unsigned) field(make_field_spec(4, 5), rX);
    if (devices[device].type == drum)
        block_num &= 511;

    set_file_position(device, block_num, false);
}

static void disk_in(Byte device, Cell rX, Address buffer)
{
    unsigned block_num;

    block_num = (unsigned) field(make_field_spec(4, 5), rX);
    if (devices[device].type == drum)
        block_num &= 511;

    set_file_position(device, block_num, false);
    read_block(device, buffer);
}

static void disk_out(Byte device, Cell rX, Address buffer)
{
    unsigned block_num;

    block_num = (unsigned) field(make_field_spec(4, 5), rX);
    if (devices[device].type == drum)
        block_num &= 511;

    set_file_position(device, block_num, true);
    write_block(device, buffer);
}

/* --- The console (typewriter/paper tape) --- */
/* Always connected to stdin/stdout, for simplicity. */

static void console_in(Byte device, Cell rX, Address buffer)
{
    read_line(stdin, buffer, block_size(device));
}

static void console_out(Byte device, Cell rX, Address buffer)
{
    write_line(stdout, buffer, block_size(device), true);
}

/* --- redirect reader/punc/printer to files --- */

void io_redirect(void)
{
   redirect_devices = 1;
}

/* --- GO button support --- */

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

/* --- time dependent I/O --- */

void do_scheduled_io(unsigned clocks)
{
    int i;
    Byte device;

    if (dbg > 3) {
        fflush(stdout);
        fprintf(stderr,"do_scheduled_io(%d)\n", clocks);
    }
    for (i = 0; i < clocks; i++) {
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

/* --- I/O subsystem init/finish --- */

void io_init(void)
{
    Byte i;

    incomplete = malloc(sizeof(int) * memory_size);
    if (!incomplete)
        fatal_error("Out of heap space!");

    control_incomplete = malloc(sizeof(int) * memory_size);
    if (!control_incomplete)
        fatal_error("Out of heap space!");

    for (i = 0; i < num_devices; i++) {
        devices[i].busy = 0;
        devices[i].int_request = false;
        devices[i].int_pending = false;
        devices[i].rewound = devices[i].type == tape ? true : false;
    }
    for (i = 0; i < memory_size; i++) {
        incomplete[i] = -WRITE;
        control_incomplete[i] = -WRITE;
    }
}

unsigned io_complete(void)
{
    unsigned clocks = 0;

    while (next_scheduled_io) {
        do_scheduled_io(1);
        clocks++;
        idle_time++;
    }
    return clocks;
}

void io_shutdown(void)
{
    Byte device;

    for (device = 0; device < num_devices; device++) {
        if (devices[device].file) {
            fclose(devices[device].file);
            devices[device].file = NULL;
        }
    }
}

/* --- I/O incomplete support --- */

Flag io_incomplete(Address address, Access access)
{
    Flag ret = false;
    int count;
    
    if (memory_size <= magnitude(address))
        error("Address out of range -- io_incomplete");
        
    count = fetch_incomplete(address);
    if (count == -WRITE)                    /* it is free */
        ret = false;
    else if (access == write_access)        /* either written to, or read from , therefore not writable */
        ret = false;
    else if (access == read_access)         /* if read from, then readable */
        ret = count ? true : false;
    return ret;
}

/* --- I/O interrupt support --- */

Flag io_pending_interrupt(Byte *int_device)
{
    Byte device, next_device;
    enum DeviceType next_device_type;

    next_device_type = (enum DeviceType) (LAST_DEVICE_TYPE + 1);
    next_device      = DEV_UNK;
    for (device = 0; device < num_devices; device++) {
        if (devices[device].busy || false == devices[device].int_pending)
            continue;
        /* select device by priority */
        if (devices[device].type < next_device_type) {
            next_device_type = devices[device].type;
            next_device = device;
        }
    }
    if (DEV_UNK != next_device) {
        devices[next_device].int_request = false;
        devices[next_device].int_pending = false;
        *int_device = next_device;
        return true;
    }
    return false;
}

/* --- Punch object card deck --- */

static void punch_card(FILE *fdev, const char *title, Address address, int num_words)
{
    static char overpunch[] = "~JKLMNOPQR";
    int i;
    char buf[16];
    Cell cell;

    fprintf(fdev, "%-5s%1d%04d", title, num_words, address);
    for (i = 0; i < num_words; i++) {
        if (memory_size <= address + i)
            break;
        cell = memory_fetch (address + i);
        sprintf(buf, "%010ld", magnitude (cell));
        if (is_negative (cell))
            buf[9] = overpunch[buf[9]-'0'];
        fprintf(fdev, "%s", buf);
    }

    for (i = num_words; i < 7; i++)
        fprintf(fdev, "          ");

    fprintf(fdev, "\n");
}

void punch_object_deck(const char *title, Address start_address)
{
    FILE *card_punch;
    Address address;
    Cell cell;
    unsigned num_cards;
    int i, num_words;

    card_punch = fopen (methods[card_out].base_filename, methods[card_out].fam);
    if (NULL == card_punch)
        error ("Cannot open card puncher");
    num_cards = 0;
    for (address = 100; address < memory_size; address++) {
        num_words = 0;
        for (i = 0; i < 7; i++) {
            if (memory_size <= address + i)
                break;
            cell = memory_fetch (address);
            if (magnitude (cell))
               num_words = i + 1;
        }
        if (num_words) {
            punch_card (card_punch, title, address, num_words);
            num_cards++;
            address += 6;
        }
    }
    if (num_cards) {
        fprintf (card_punch, "TRANS0%04d\n", abs(start_address));
        fflush (card_punch);
    }
    fclose (card_punch);
}

/* vim: set ts=4 sw=4 et: */

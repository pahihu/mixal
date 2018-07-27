/* MIX simulator, copyright 1994 by Darius Bacon */ 
#include "mix.h"
#include "asm.h"    /* for entry_point */
#include "charset.h"
#include "io.h"
#include "run.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void xecute(Address address, Flag save_context);

static void save_memory(const char *path, Cell *store, unsigned size)
{
    FILE *fout;
    size_t cells_written;

    fout = fopen(path, "wb");
    if (NULL == fout)
        error("Cannot open %s", path);
    cells_written = fwrite(store, sizeof(Cell), size, fout);
    fclose(fout);
    if (cells_written != size)
        error("Cannot write %d cells to %s", size, path);
}

static void load_memory(const char *path, Cell *store, unsigned size)
{
    FILE *fin;

    fin = fopen(path, "rb");
    if (NULL == fin)
        return;
    fread(store, sizeof(Cell), size, fin);
    fclose(fin);
}

static void stop(const char *message, va_list args)
{
    fprintf(stderr, "RUNTIME ERROR: ");
    vfprintf(stderr, message, args);
    fprintf(stderr, "\n");
    print_CPU_state();
    exit(1);
}

/* --- Execution statistics --- */

static unsigned long elapsed_time = 0;      /* in Tyme units */

/* --- The CPU state --- */

Cell memory[memory_size];
Cell control_memory[memory_size];            /* control store    */
unsigned frequency[memory_size];            /* frequency counts */
unsigned control_frequency[memory_size];    /* control store frequency counts */
unsigned trace_count = 0;                   /* do not trace instructions */
unsigned mix_config = 0;                    /* MIX configuration */

#define A 0
#define X 7
#define J 8
static Cell r[10];      /* the registers; except that r[9] == zero. */

static int comparison_indicator;    /* the overflow toggle is defined in cell.c */
static Address pc;                  /* the program counter */
static State internal_state;        /* MIX internal state  */
static int rtc_busy;                /* real-time clock active */
static Flag rtc_int_pending;        /* real-time clock interrupt pending */

void set_trace_count(unsigned value)
{
    trace_count = value;
}

void set_initial_state(void)
{
    /* --- Initialize I/O system --- */
    io_init();

    overflow = false;
    comparison_indicator = 0;
    internal_state = (mix_config & MIXCONFIG_INTERRUPT)
            ? control_state 
            : normal_state;

    /* --- Init real-time clock --- */
    rtc_busy = 0;
    rtc_int_pending = false;
    {
    unsigned i;
    for (i = 0; i < 10; ++i)
        r[i] = zero;
    }
    pc = entry_point;       /*** need to check for no entry point */
}

State get_internal_state(void)
{
    return internal_state;
}

static void init_configuration(void)
{
    /* --- Load core memory --- */
    if (mix_config & MIXCONFIG_CORE) {
        if (mix_config & MIXCONFIG_INTERRUPT)
            load_memory("mixcore.ctl", control_memory, memory_size);
        load_memory("mixcore.dat", memory, memory_size);
    }
}

void set_configuration(unsigned flags)
{
    if (MIXCONFIG_MASTER & flags)
        flags = (unsigned) -1;
    mix_config |= flags;

    init_configuration();
}

static void check_master(void)
{
    if (0 == (mix_config & MIXCONFIG_MASTER))
        error("Not a Mixmaster");
}

static void check_binary(void)
{
    if (0 == (mix_config & MIXCONFIG_BINARY))
        error("Not a binary MIX");
}

static void check_interrupt(void)
{
    if (0 == (mix_config & MIXCONFIG_INTERRUPT))
        error("No interrupt facility installed");
}

static void check_float(void)
{
    if (0 == (mix_config & MIXCONFIG_FLOAT))
        error("No floating point attachment installed");
}

static unsigned fetch_frequency(Address address)
{
    return address < 0 ? control_frequency[-address] : frequency[address];
}

static void increment_frequency(Address address)
{
    if (address < 0)
        control_frequency[-address]++;
    else
        frequency[address]++;
}

#define SIGN(x)        ((x) < 0 ? -1 : (x) > 0 ? 1 : 0)

static void save_state(void)
{
    Cell psw;
    Byte flags;
    int i;

    control_memory[9] = r[A];
    control_memory[8] = r[X];
    for (i = 1; i < 7; i++)
        control_memory[8 - i] = r[i];

    flags = 0;
    if (overflow)
        flags += 8;
    flags += 1 + SIGN(comparison_indicator);

    psw = set_field(r[J], make_field_spec(4, 5), zero);
    psw = set_byte(flags, 3, psw);
    psw = set_field(address_to_cell(pc), make_field_spec(1, 2), psw);

    control_memory[1] = psw;
}

static void restore_state(void)
{
    Cell psw;
    Byte flags;
    int i;

    r[A] = control_memory[9];
    r[X] = control_memory[8];
    for (i = 1; i < 7; i++)
        r[i] = control_memory[8 - i];
    
    psw  = control_memory[1];
    r[J] = field(make_field_spec(4, 5), psw);
    pc   = cell_to_address(field(make_field_spec(1, 2), psw));

    flags  = get_byte(3, psw);
    overflow = flags > 7 ? true : false;
    flags &= 7;
    comparison_indicator = (int) flags - 1;
}

static void print_pc(void)
{
    printf("%c%04o", pc < 0 ? '-' : ' ', abs(pc));
}

void print_CPU_state(void)
{
    printf ("A:");
    print_cell (r[A]);
    printf ("\t");
    {                        /* Print the index registers: */
        unsigned i;
        for (i = 1; i <= 6; ++i)
        printf ("I%u:%s%04lo  ",
            i, is_negative (r[i]) ? "-" : " ", magnitude (r[i]));
    }
    printf ("\nX:");
    print_cell (r[X]);
    printf ("\t J: %04lo", magnitude (r[J]));    /* (it's always nonnegative) */
    printf ("  PC:"); print_pc();
    printf ("  Flags: %-7s %-8s %-7s",
        comparison_indicator < 0 ? "less" :
        comparison_indicator == 0 ? "equal" : "greater",
    overflow ? "overflow" : "",
        control_state == internal_state ? "control" : "");
    printf (" %11lu elapsed (%lu idle)\n", elapsed_time, idle_time);
}

/* --- Memory access --- */

Cell memory_fetch(Address address)
{
    Cell cell;

    if (memory_size <= abs(address))
        error("Address out of range");

    if (address < 0) {
        check_interrupt();
        if (normal_state == internal_state)
            error("Cannot access control store in normal state");

        cell = control_memory[-address];
    }
    else
        cell = memory[address];

    return cell;
}

void memory_store(Address address, Cell cell)
{
    if (memory_size <= abs(address))
        error("Address out of range");

    if (address < 0) {
    check_interrupt();
        if (normal_state == internal_state)
            error("Cannot access control store in normal state");

        memory[-address] = cell;
        if (-10 == address)
            rtc_busy = 1000;
    } else
        memory[address] = cell;
}

/* --- I/O protected memory access */
static Cell safe_fetch(Address address)
{
    if (io_incomplete(address, read_access))
        error("Incomplete I/O -- safe_fetch");
    return memory_fetch(address);
}

static void safe_store(Address address, Cell cell)
{
    if (io_incomplete(address, write_access))
        error("Incomplete I/O -- safe_store");
    memory_store(address, cell);
}

/* --- The interpreter --- */

/* --- I've followed Knuth's MIX interpreter quite closely. */

static jmp_buf escape_k;    /* continuation to escape from interpreter */

/* C, F, M, and V as defined in Knuth: */

static Byte C;
static Byte F;
static Cell M;
static Flag wait_state;         /* wait state */
static Flag rti_done;           /* return from interrupt executed */

void set_wait_state(void)
{
    wait_state = true;
}

void print_DUMP(void)
{
    int i, j;
    Address address = 0;
    
    printf ("\n");
    printf ("CONTENTS OF MIX MEMORY (NONZERO LOCATIONS ONLY)\n\n");
    printf ("LOC        0           1           2           3              FREQUENCY COUNTS\n");
    for (i = 0; i < memory_size / 4; i++, address += 4) {
        Flag has_nonzero = false;
        for (j = 0; !has_nonzero && j < 4; j++) {
            Cell value = memory_fetch(address + j);
            if (magnitude(value))
                has_nonzero = true;
        }
        if (!has_nonzero)
            continue;
        printf ("%04o:", address);
        for (j = 0; j < 4; j++) {
            Cell value = memory_fetch(address + j);
            if (magnitude(value)) {
                printf (" ");
                print_cell (value);
            }
            else
                printf (" %11s", "");
        }
        printf("     ");
        for (j = 0; j < 4; j++)
            printf(" %05d", frequency[address + j]);
        printf("\n");
    }
}

static Cell get_V(void)
{
    return field(F, safe_fetch(cell_to_address(M)));
}

/* do_foo performs the action of instruction type foo. */

static void do_nop(void)    { }

static void do_add(void)
{
    if (6 == F) {
        check_float();
        r[A] = float_add(r[A], safe_fetch(cell_to_address(M)));
    } else
        r[A] = add(r[A], get_V());
}

static void do_sub(void)
{
    if (6 == F) {
        check_float();
        r[A] = float_subtract(r[A], safe_fetch(cell_to_address(M)));
    } else
        r[A] = sub(r[A], get_V());
}

static void do_mul(void)
{
    if (6 == F) {
        check_float();
        r[A] = float_multiply(r[A], safe_fetch(cell_to_address(M)));
    } else
        multiply(r[A], get_V(), &r[A], &r[X]);
}

static void do_div(void)
{
    if (6 == F) {
        check_float();
        r[A] = float_divide(r[A], safe_fetch(cell_to_address(M)));
    } else
        divide(r[A], r[X], get_V(), &r[A], &r[X]);
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

static void do_special(void)
{
    switch (F) {
    case 0: { /* NUM */
        unsigned i;
        Cell num = zero;
        Cell ten = ulong_to_cell(10);
        if (overpunched(r[X])) {
            r[X] = flip_chars(r[X]);
            r[X] = flip_chars(r[A]);
        }
        for (i = 1; i <= 5; ++i)
            num = add(mul(ten, num), (Cell)(get_byte(i, r[A]) % 10));
        for (i = 1; i <= 5; ++i)
            num = add(mul(ten, num), (Cell)(get_byte(i, r[X]) % 10));
        r[A] = is_negative(r[A]) ? negative(num) : num;
        break;
    }
    case 1: { /* CHAR */
        unsigned long num = magnitude(r[A]);
        unsigned z = (unsigned) C_char_to_mix('0');
        unsigned i;
        for (i = 5; 0 < i; --i, num /= 10)
            r[X] = set_byte((Byte) (z + num % 10), i, r[X]);
        for (i = 5; 0 < i; --i, num /= 10)
            r[A] = set_byte((Byte) (z + num % 10), i, r[A]);
        break;
    }
    case 2: /* HLT */
        elapsed_time += io_complete();
        longjmp(escape_k, 1);
        break;
    case 3: /* AND */
        check_binary();
        r[A] = logical_product(r[A], get_V());
        break;
    case 4: /* OR  */
        check_binary();
        r[A] = logical_sum(r[A], get_V());
        break;
    case 5: /* XOR */
        check_binary();
        r[A] = logical_difference(r[A], get_V());
        break;
    case 6: /* FLOT */
        check_float();
        r[A] = float_flot(r[A]);
        break;
    case 7: /* FIX  */
        check_float();
        r[A] = float_fix(r[A]);
        break;
    case 8: { /* NEG */
        check_binary();
        unsigned long cell = ~magnitude(r[A]);
        r[A] = is_negative(r[A]) ? negative(cell) : cell;
        break;
    }
    case 9: { /* XCH */
        check_binary();
        Cell cell = r[A];
        r[A] = r[X]; r[X] = cell;
        break;
    }
    case 10: { /* XEQ */
        check_master();
        xecute(cell_to_address(M), true);
        break;
    }
    default: error("Unknown extended opcode");
    }
}

static void do_shift(void)
{
    Cell ignore;
    unsigned long count = magnitude(M);

    if (is_negative(M) && count != 0)
        error("Negative shift count");
    switch (F) {
    case 0: /* SLA */
        shift_left(zero, r[A], count, &ignore, &r[A]);
        break;
    case 1: /* SRA */
        shift_right(r[A], zero, count, &r[A], &ignore);
        break;
    case 2: /* SLAX */
        shift_left(r[A], r[X], count, &r[A], &r[X]);
        break;
    case 3: /* SRAX */
        shift_right(r[A], r[X], count, &r[A], &r[X]);
        break;
    case 4: /* SLC  */
        shift_left_circular(r[A], r[X], (unsigned)(count % 10), &r[A], &r[X]);
        break;
    case 5: { /* SRC */
        unsigned c = (10 - count % 10) % 10;    /* -count modulo 10 */
        shift_left_circular(r[A], r[X], c, &r[A], &r[X]);
        break;
    }
    case 6: { /* SLB */
        check_binary();
        shift_left_binary(r[A], r[X], count, &r[A], &r[X]);
        break;
    }
    case 7: { /* SRB */
        check_binary();
        shift_right_binary(r[A], r[X], count, &r[A], &r[X]);
        break;
    }
    case 9: { /* INT */
        check_interrupt();
        if (normal_state == internal_state) {
            save_state();
            internal_state = control_state;
            pc = -12;
        } else {
            restore_state();
            internal_state = normal_state;
            rti_done = true;
        }
        elapsed_time++;
        break;
    }
    default: error("Unknown extended opcode");
    }
}

static void do_move(void)
{
    Address from = cell_to_address(M);
    Address to = cell_to_address(r[1]);
    unsigned i, count = F;

    for (i = count; i != 0; --i) {
        safe_store(to + i, safe_fetch(from + i));
        elapsed_time += 2;
    }
    r[1] = address_to_cell(to + count);
}

static void do_lda(void)    { r[A] = get_V(); }
static void do_ldx(void)    { r[X] = get_V(); }
static void do_ldi(void) { 
    Cell cell = get_V();
    if (INDEX_MAX < magnitude(cell))
        error("Magnitude too large for index register: %10o", magnitude(cell));
    r[C & 7] = cell; 
}

static void do_ldan(void)   { r[A] = negative(get_V()); }
static void do_ldxn(void)   { r[X] = negative(get_V()); }
static void do_ldin(void) {
    Cell cell = get_V();
    if (INDEX_MAX < magnitude(cell))
        error("Magnitude too large for index register: %10o", magnitude(cell));
    r[C & 7] = negative(cell);
}

static void do_store(void)
{
    Address a = cell_to_address(M);
    // memory[a] = set_field(r[C-24], F, memory[a]);
    safe_store(a, set_field(r[C-24], F, safe_fetch(a)));
}

static void jump(void)
{
    r[J] = address_to_cell(pc);
    pc = cell_to_address(M);
}

static void branch(unsigned condition, int sign, Cell cell)
{
    switch (condition) {
    case  0: jump(); break;
    case  1: pc = cell_to_address(M); break;
    case  2: if (overflow)  jump(); overflow = false; break;
    case  3: if (!overflow) jump(); overflow = false; break;
    case  4: if (sign <  0) jump(); break;
    case  5: if (sign == 0) jump(); break;
    case  6: if (sign  > 0) jump(); break;
    case  7: if (sign >= 0) jump(); break;
    case  8: if (sign != 0) jump(); break;
    case  9: if (sign <= 0) jump(); break;
    case 10: check_binary(); if (0 == (magnitude(cell) & 2)) jump(); break;
    case 11: check_binary(); if (1 == (magnitude(cell) & 2)) jump(); break;
    default: error("Bad branch condition");
    }
}

static void do_jump(void)
{
    branch(F, comparison_indicator, zero);
}

static int sign_of_difference(Cell difference)
{
    return magnitude(difference) == 0 ? 0 : is_negative(difference) ? -1 : 1;
}

static void do_reg_branch(void)
{
    Cell cell;

    cell = r[C & 7];
    branch(F + 4, sign_of_difference(cell), cell);
}

static void do_jbus(void)
{
    /* simulated devices with io and access time using C's blocking I/O */
    if (io_device_busy(F))
        jump();
}

static void do_jred(void)
{
    if (!io_device_busy(F))
        jump();
}

static void do_ioc(void)    { elapsed_time += io_control(F, r[X], M); }
static void do_in(void)     { elapsed_time += do_input(F, r[X], cell_to_address(M)); }
static void do_out(void)    { elapsed_time += do_output(F, r[X], cell_to_address(M)); }

static void compare(Cell cell)
{
    Flag saved = overflow;

    Cell difference = sub(field(F, r[C & 7]), field(F, cell));
    comparison_indicator = sign_of_difference(difference);
    overflow = saved;
}

static void do_addr_op(void)
{
    Cell cell;
    unsigned reg = C & 7;

    switch (F) {
    case 0: cell = add(r[reg], M); break;
    case 1: cell = sub(r[reg], M); break;
    case 2: cell = M; break;
    case 3: cell = negative(M); break;
    case 4: check_master(); compare(M); return;
    default: error("Unknown extended opcode"); cell = zero;
    }
    if (reg - 1 < 6)        /* same as: 1 <= reg && reg <= 6 */
        if (INDEX_MAX < magnitude(cell))
            error("Magnitude too large for index register: %10o", 
              magnitude(cell));
    r[reg] = cell;
}

static void do_compare(void)
{
    Cell cell = safe_fetch(cell_to_address(M));

    if (6 == F) {
        check_float();
        comparison_indicator = float_compare(r[A], cell);
    } else
        compare(cell);
}

static const struct {
    void (*action)(void);
    unsigned clocks;
    const char *mnemonic;
} op_table[64] = {
    { do_nop, 1, "NOP" },
    { do_add, 2, "%ADD ADD ADD ADD ADD ADD FADDU017" },
    { do_sub, 2, "%SUB SUB SUB SUB SUB SUB FSUBU027" },
    { do_mul, 10, "%MUL MUL MUL MUL MUL MUL FMULU037" },
    { do_div, 12, "%DIV DIV DIV DIV DIV DIV FDIVU047" },
    { do_special, 1, "*NUM CHARHLT AND OR  XOR FLOTFIX NEG XCH " },
    { do_shift, 2, "*SLA SRA SLAXSRAXSLC SRC SLB SRB U068INT " },
    { do_move, 1, "MOV" },

    { do_lda, 2, "LDA" },
    { do_ldi, 2, "LD1" },
    { do_ldi, 2, "LD2" },
    { do_ldi, 2, "LD3" },
    { do_ldi, 2, "LD4" },
    { do_ldi, 2, "LD5" },
    { do_ldi, 2, "LD6" },
    { do_ldx, 2, "LDX" },

    { do_ldan, 2, "LDAN" },
    { do_ldin, 2, "LD1N" },
    { do_ldin, 2, "LD2N" },
    { do_ldin, 2, "LD3N" },
    { do_ldin, 2, "LD4N" },
    { do_ldin, 2, "LD5N" },
    { do_ldin, 2, "LD6N" },
    { do_ldxn, 2, "LDXN" },

    { do_store, 2, "STA" },
    { do_store, 2, "ST1" },
    { do_store, 2, "ST2" },
    { do_store, 2, "ST3" },
    { do_store, 2, "ST4" },
    { do_store, 2, "ST5" },
    { do_store, 2, "ST6" },
    { do_store, 2, "STX" },

    { do_store, 2, "STJ" },
    { do_store, 2, "STZ" },
    { do_jbus, 1, "JBUS" },
    { do_ioc, 1, "IOC" },
    { do_in, 1, "IN" },
    { do_out, 1, "OUT" },
    { do_jred, 1, "JRED" },
    { do_jump, 1, "*JMP JSJ JOV JNOVJL  JE  JG  JGE JNE JLE " },

    { do_reg_branch, 1, "*JAN JAZ JAP JANNJANZJANPJAE JAO " },
    { do_reg_branch, 1, "*J1N J1Z J1P J1NNJ1NZJ1NPJ1E J1O " },
    { do_reg_branch, 1, "*J2N J2Z J2P J2NNJ2NZJ2NPJ2E J2O" },
    { do_reg_branch, 1, "*J3N J3Z J3P J3NNJ3NZJ3NPJ3E J3O" },
    { do_reg_branch, 1, "*J4N J4Z J4P J4NNJ4NZJ4NPJ4E J4O" },
    { do_reg_branch, 1, "*J5N J5Z J5P J5NNJ5NZJ5NPJ5E J5O" },
    { do_reg_branch, 1, "*J6N J6Z J6P J6NNJ6NZJ6NPJ6E J6O" },
    { do_reg_branch, 1, "*JXN JXZ JXP JXNNJXNZJXNPJXE JXO" },

    { do_addr_op, 1, "*INCADECAENTAENNACPAM" },
    { do_addr_op, 1, "*INC1DEC1ENT1ENN1CP1M" },
    { do_addr_op, 1, "*INC2DEC2ENT2ENN2CP2M" },
    { do_addr_op, 1, "*INC3DEC3ENT3ENN3CP3M" },
    { do_addr_op, 1, "*INC4DEC4ENT4ENN4CP4M" },
    { do_addr_op, 1, "*INC5DEC5ENT5ENN5CP5M" },
    { do_addr_op, 1, "*INC6DEC6ENT6ENN6CP6M" },
    { do_addr_op, 1, "*INCXDECXENTXENNXCPXM" },

    { do_compare, 2, "%CMPACMPACMPACMPACMPACMPAFCMPU707" },
    { do_compare, 2, "CMP1" },
    { do_compare, 2, "CMP2" },
    { do_compare, 2, "CMP3" },
    { do_compare, 2, "CMP4" },
    { do_compare, 2, "CMP5" },
    { do_compare, 2, "CMP6" },
    { do_compare, 2, "CMPX" },
};

static const char* mnemonic(Byte C, Byte F)
{
    static char buffer[5];
    const char *ret = op_table[C].mnemonic;
    
    if (*ret == '*') {
        strncpy(buffer, ret + 1 + F * 4, 4);
        buffer[4] = '\0';
        ret = buffer;
    } else if (*ret == '%') {
        strncpy(buffer, ret + 1 + (7 & F) * 4, 4);
        buffer[4] = '\0';
        ret = buffer;
    }
    return ret;
}

void print_CPU_trace(Flag header)
{
    Byte I = 0;
    Cell instruction;
    
    IGNORE_VALUE(I);

    if (header) {
        printf ("  LOC FREQ INSTRUCTION OP    OPERAND   |  REGISTER A  REGISTER X  RI1   RI2   RI3   RI4   RI5   RI6   RJ   VCS   TYME\n");
        // printf ("+1000 0001 +1234567890 JBUS +1234567890| +1234567890 +1234567890 +0000 +0000 +0000 +0000 +0000 +0000 +1007 XEC 00014435\n");
        return;
    }

    instruction = safe_fetch(pc);
    destructure_cell(instruction, M, I, F, C);
    print_pc();
    printf (" %04d ", fetch_frequency(pc));
    print_cell (instruction);
    printf (" %-4s ", mnemonic (C, F));
    print_cell (M);
    printf ("| ");
    print_cell (r[A]);
    printf (" ");
    print_cell (r[X]);
    {
        unsigned i;
        for (i = 1; i <= 6; ++i)
        printf (" %s%04lo", is_negative (r[i]) ? "-" : " ", magnitude (r[i]));
    }
    printf (" +%04lo", magnitude (r[J]));
    printf (" %c%c%c ", " X"[overflow ? 1: 0],
               "LEG"[1 + SIGN(comparison_indicator)],
                        " C"[control_state == internal_state ? 1 : 0]);
    printf (" %08lu\n", elapsed_time);
}

static Cell effective_address(Address location)
{
    Byte II, I1, I2, CC = 0, FF = 0;
    Cell MM;

    Cell E, H;
    Address L;
    long N;

    IGNORE_VALUE(CC);
    IGNORE_VALUE(FF);

    H = zero; L = location; N = 0;
A2:
    destructure_cell(safe_fetch(L), MM, II, FF, CC);
    E = MM;
    I2 = II &  7;
    I1 = II >> 3;
    if (0 < I1 && I1 < 7) E = add(E, r[I1]);
    if (0 < I2 && I2 < 7) H = add(H, r[I2]);
    if (I1 == 7 && I2 == 7) {
        N++; H = zero;
    }
/* A3: */
    if (I1 == 7 || I2 == 7) {
        elapsed_time++;
        L = cell_to_address(E); goto A2;
    }
    else {
        E = add(E, H); H = zero;
    }
/* A4: */
    if (N > 0) {
        L = E; N--; goto A2;
    }

    return E;
}

static void xecute(Address address, Flag save_context)
{
    Byte I;
    Byte saved_C = 0;
    Byte saved_F = 0;
    Cell saved_M = zero;
    Address saved_pc = 0;

    if (true == save_context) {
        saved_C = C;
        saved_F = F;
        saved_M = M;
        saved_pc = pc;
        pc = address;
    }

    destructure_cell(safe_fetch(pc), M, I, F, C);

    /* --- Indirect addressing --- */
    if (I != 0) {
       if (I < 7)
           /* (the add can't overflow because the numbers are too small) */
           M = add(M, r[I]);
       else {
           if (0 == (mix_config & MIXCONFIG_MASTER))
               error("Not a Mixmaster. Invalid I-field: %02o", I);
           M = effective_address(pc);
       }
    }

    wait_state = false;
    rti_done = false;
    increment_frequency(pc); pc++;
    op_table[C].action();
    if (save_context)
        elapsed_time += op_table[C].clocks;

    if (true == wait_state)
        pc--;

    if (save_context) {
        C  = saved_C;
        F  = saved_F;
        M  = saved_M;
        pc = saved_pc;
    }
}

void run(void)
{
    install_error_handler(stop);
    if (setjmp(escape_k) != 0) {
        if (mix_config & MIXCONFIG_CORE) {
            if (mix_config & MIXCONFIG_INTERRUPT)
                save_memory("mixcore.ctl", control_memory, memory_size);
            save_memory("mixcore.dat", memory, memory_size);
        }
        return;
    }

    /* --- Go device support --- */
    if (MIXCONFIG_PUSHGO & mix_config) {
        M = 0; F = io_go_device(); C = 36;
        op_table[C].action();
        io_complete();
        pc = 0;
    }

    if (trace_count)
        print_CPU_trace(true);
    for (;;) {
        if (memory_size <= abs(pc))
            error("Program counter out of range: %c%04o", pc < 0 ? '-' : ' ', abs(pc));
        {
            unsigned long start_time, delta_time;

            /* --- Instruction trace --- */
            if (trace_count && (frequency[pc] <= trace_count))
                print_CPU_trace(false);

            /* MOV adds 2 clocks per word */
            start_time = elapsed_time;
            xecute(pc, false);
            elapsed_time += op_table[C].clocks;

            /* --- I/O subsystem --- */
            delta_time = elapsed_time - start_time;
            if ((false == wait_state) && io_scheduled())
                do_scheduled_io(delta_time);

            /* --- Real-time clock --- */
            if (rtc_busy) {
                if (rtc_busy <= delta_time) {
                    Cell cell = sub(control_memory[10], ulong_to_cell(1));
                    control_memory[10] = cell;
                    if (magnitude(cell))
                        rtc_busy = 1000 + rtc_busy - delta_time;
                    else
                        rtc_busy = 0;
                    rtc_int_pending = true;
                }
            } else
                rtc_busy -= delta_time;

            /* --- Interrupt facility --- */
            if (!rti_done && 
                normal_state == internal_state)
            {
                Address int_pc = 0;
                Byte device;

                if (true == rtc_int_pending) {
                    rtc_int_pending = false;
                    int_pc = -11;
                }
                else if (true == io_pending_interrupt(&device))
                    int_pc = -(20 + device);
                if (int_pc) {
                    save_state();
                    internal_state = control_state;
                    pc = int_pc;
                }
            }
        } /* for (;;) body */
    } /* for (;;) */
}


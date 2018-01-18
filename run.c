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
unsigned frequency[memory_size];            /* frequency counts */
unsigned trace_count = 0;                   /* do not trace instructions */

#define A 0
#define X 7
#define J 8
static Cell r[10];      /* the registers; except that r[9] == zero. */

static int comparison_indicator;    /* the overflow toggle is defined in cell.c */
static Address pc;      /* the program counter */

void set_trace_count(unsigned value)
{
    trace_count = value;
}

void set_initial_state(void)
{
    io_init();

    overflow = false;
    comparison_indicator = 0;
    {
	unsigned i;
	for (i = 0; i < 10; ++i)
	    r[i] = zero;
    }
    pc = entry_point;       /*** need to check for no entry point */
}

void print_CPU_state(void)
{
    printf ("A:");
    print_cell (r[A]);
    printf ("\t");
    {				/* Print the index registers: */
        unsigned i;
        for (i = 1; i <= 6; ++i)
	        printf ("I%u:%s%04lo  ",
		        i, is_negative (r[i]) ? "-" : " ", magnitude (r[i]));
    }
    printf ("\nX:");
    print_cell (r[X]);
    printf ("\t J: %04lo", magnitude (r[J]));	/* (it's always nonnegative) */
    printf ("  PC: %04o", pc);
    printf ("  Flags: %-7s %-8s",
	    comparison_indicator < 0 ? "less" :
	      comparison_indicator == 0 ? "equal" : "greater",
	    overflow ? "overflow" : "");
    printf (" %11lu elapsed (%lu idle)\n", elapsed_time, idle_time);
}

/* --- Memory access --- */

Cell memory_fetch(Address address)
{
	if (memory_size <= address)
	    error("Address out of range");
    return memory[address];
}

void memory_store(Address address, Cell cell)
{
    if (memory_size <= address)
	    error("Address out of range");
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
        for (j = 0; j < 4; j++) {
            printf(" %05d", frequency[address + j]);
        }
        printf("\n");
    }
}

static Cell get_V(void)
{
    return field(F, safe_fetch(cell_to_address(M)));
}

/* do_foo performs the action of instruction type foo. */

static void do_nop(void)    { }

static void do_add(void)    { r[A] = add(r[A], get_V()); }
static void do_sub(void)    { r[A] = sub(r[A], get_V()); }

static void do_mul(void)    { multiply(r[A], get_V(), &r[A], &r[X]); }
static void do_div(void)    { divide(r[A], r[X], get_V(), &r[A], &r[X]); }

static void do_special(void)
{
    switch (F) {
	case 0: { /* NUM */
	    unsigned i;
	    Cell num = zero;
	    Cell ten = ulong_to_cell(10);
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
            do { do_scheduled_io(1); } while (io_scheduled());
	        longjmp(escape_k, 1);
            break;
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
	default: error("Unknown extended opcode");
    }
}

static void do_move(void)
{
    Address from = cell_to_address(M);
    Address to = cell_to_address(r[1]);
    unsigned count = F;
    for (; count != 0; --count) {
	    // if (memory_size <= from + count || memory_size <= to + count)
	        // error("Address out of range");
	    // memory[to + count] = memory[from + count];
	    safe_store(to + count, safe_fetch(from + count));
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

static void branch(unsigned condition, int sign)
{
    switch (condition) {
	case 0: jump(); break;
	case 1: pc = cell_to_address(M); break;
	case 2: if (overflow)  jump(); overflow = false; break;
	case 3: if (!overflow) jump(); overflow = false; break;
	case 4: if (sign <  0) jump(); break;
	case 5: if (sign == 0) jump(); break;
	case 6: if (sign  > 0) jump(); break;
	case 7: if (sign >= 0) jump(); break;
	case 8: if (sign != 0) jump(); break;
	case 9: if (sign <= 0) jump(); break;
	default: error("Bad branch condition");
    }
}

static void do_jump(void)
{
    branch(F, comparison_indicator);
}

static int sign_of_difference(Cell difference)
{
    return magnitude(difference) == 0 ? 0 : is_negative(difference) ? -1 : 1;
}

static void do_reg_branch(void)
{
    branch(F + 4, sign_of_difference(r[C & 7]));
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

static void do_ioc(void)    { io_control(F, r[X], M); }
static void do_in(void)     { do_input(F, r[X], cell_to_address(M)); }
static void do_out(void)    { do_output(F, r[X], cell_to_address(M)); }

static void do_addr_op(void)
{
    Cell cell;
    unsigned reg = C & 7;
    switch (F) {
	case 0: cell = add(r[reg], M); break;
	case 1: cell = sub(r[reg], M); break;
	case 2: cell = M; break;
	case 3: cell = negative(M); break;
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
    Flag saved = overflow;
    Cell difference = sub(field(F, r[C & 7]), 
			  field(F, safe_fetch(cell_to_address(M))));
    comparison_indicator = sign_of_difference(difference);
    overflow = saved;
}

static const struct {
    void (*action)(void);
    unsigned clocks;
    const char *mnemonic;
} op_table[64] = {
    { do_nop, 1, "NOP" },
    { do_add, 2, "ADD" },
    { do_sub, 2, "SUB" },
    { do_mul, 10, "MUL" },
    { do_div, 12, "DIV" },
    { do_special, 1, "*NUM CHARHLT " },
    { do_shift, 2, "*SLA SRA SLAXSRAXSLC SRC " },
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

    { do_reg_branch, 1, "*JAN JAZ JAP JANNJANZJANP" },
    { do_reg_branch, 1, "*J1N J1Z J1P J1NNJ1NZJ1NP" },
    { do_reg_branch, 1, "*J2N J2Z J2P J2NNJ2NZJ2NP" },
    { do_reg_branch, 1, "*J3N J3Z J3P J3NNJ3NZJ3NP" },
    { do_reg_branch, 1, "*J4N J4Z J4P J4NNJ4NZJ4NP" },
    { do_reg_branch, 1, "*J5N J5Z J5P J5NNJ5NZJ5NP" },
    { do_reg_branch, 1, "*J6N J6Z J6P J6NNJ6NZJ6NP" },
    { do_reg_branch, 1, "*JXN JXZ JXP JXNNJXNZJXNP" },

    { do_addr_op, 1, "*INCADECAENTAENNA" },
    { do_addr_op, 1, "*INC1DEC1ENT1ENN1" },
    { do_addr_op, 1, "*INC2DEC2ENT2ENN2" },
    { do_addr_op, 1, "*INC3DEC3ENT3ENN3" },
    { do_addr_op, 1, "*INC4DEC4ENT4ENN4" },
    { do_addr_op, 1, "*INC5DEC5ENT5ENN5" },
    { do_addr_op, 1, "*INC6DEC6ENT6ENN6" },
    { do_addr_op, 1, "*INCXDECXENTXENNX" },

    { do_compare, 2, "CMPA" },
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
    }
    return ret;
}

void print_CPU_trace(Flag header)
{
    Byte I;
    Cell instruction;
    
    if (header) {
        printf (" LOC FREQ INSTRUCTION OP    OPERAND   |  REGISTER A  REGISTER X  RI1   RI2   RI3   RI4   RI5   RI6   RJ  OV CI    TYME\n");
        // printf ("1000 0001 +1234567890 JBUS +1234567890| +1234567890 +1234567890 +0000 +0000 +0000 +0000 +0000 +0000 +1007 X E  00014435\n");
        return;
    }

    instruction = safe_fetch(pc);
    destructure_cell(instruction, M, I, F, C);
    printf ("%04o %04d ", pc, frequency[pc]);
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
	        printf (" %s%04lo",
		                is_negative (r[i]) ? "-" : " ", magnitude (r[i]));
    }
    printf (" +%04lo", magnitude (r[J]));
    printf (" %s %s ", overflow ? "X" : " ",
                       comparison_indicator < 0 ? "L" :
                            comparison_indicator == 0 ? "E" : "G");
    printf (" %08lu\n", elapsed_time);
}

void run(void)
{
    Byte go_device = 255;

    install_error_handler(stop);
    if (setjmp(escape_k) != 0)
	return;

    go_device = io_go_device();
    if (go_device != 255) {
        M = 0; F = go_device; C = 36;
        op_table[C].action();
        pc = 0;
    }

    if (trace_count)
        print_CPU_trace(true);
    for (;;) {
/*      print_CPU_state(); */
    	if (memory_size <= pc)
    	    error("Program counter out of range: %4o", pc);
    	{
    	    Byte I;
    	    unsigned long start_time;

    	    if (trace_count && (frequency[pc] <= trace_count))
    	        print_CPU_trace(false);
    	    destructure_cell(safe_fetch(pc), M, I, F, C);
    	    if (6 < I)
    		    error("Invalid I-field: %u", I);
    	    if (I != 0)
    	        M = add(M, r[I]);  /* (the add can't overflow because the numbers are too small) */
    	    /* MOV adds 2 clocks per word */
    	    start_time = elapsed_time;
    	    frequency[pc]++; pc++;
    	    op_table[C].action();
    	    elapsed_time += op_table[C].clocks;
            if (io_scheduled())
                do_scheduled_io(elapsed_time - start_time);
    	}
    }
}

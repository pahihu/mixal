/* MIX simulator, copyright 1994 by Darius Bacon */ 
#include "mix.h"

#include "asm.h"
#include "run.h"    /* for memory_fetch(), memory_store() */

#include <stdlib.h>

/* --- The assembly buffer --- */

Address here = 0;

Cell asm_fetch_field(Address address, unsigned L, unsigned R)
{
    assert(address < memory_size);
    return field(make_field_spec(L, R), memory_fetch(address));
}

#include <stdio.h>

void asm_store_field(Address address, unsigned L, unsigned R, Cell cell)
{
    // assert(address < memory_size);
    if (VERBOSE) {
	    char temp[12];
	    unparse_cell(temp, cell);
	    printf("%4o(%u,%u): %s\n", address, L, R, temp);
    }
    memory_store(address, set_field(cell, make_field_spec(L, R), memory_fetch(address)));
}

void assemble(Cell cell)
{
    if (VERBOSE) {
        char temp[12];
	    unparse_cell(temp, cell);
	    printf("%4o: %s\n", here, temp);
    }
    // if (here < memory_size)
    //	memory[here] = cell;
    // else
	//  error("Address out of range");
	memory_store(here, cell);
    ++here;
}

Address entry_point;

void set_entry_point(Address address)
{
    assert(abs(address) < memory_size);
    if (VERBOSE)
        printf("entry point: %4o\n", address);
    entry_point = address;
}

static void punch_card(FILE *fdev, const char *title, Address address)
{
    static char overpunch[] = "~JKLMNOPQR";
    int i, numwords;
    char buf[16];
    Cell cell;

    numwords = 0;
    for (i = 0; i < 7; i++) {
        if (memory_size <= address + i)
            break;
        cell = memory_fetch(address + i);
        if (magnitude(cell)) {
            numwords = i + 1;
        }
    }

    if (0 == numwords)
        return;

    fprintf(fdev, "%-5s%1d%04d", title, numwords, address);
    for (i = 0; i < numwords; i++) {
        if (memory_size <= address + i)
            break;
        cell = memory_fetch(address + i);
        sprintf(buf, "%010ld", magnitude(cell));
        if (is_negative(cell))
            buf[9] = overpunch[buf[9]-'0'];
        fprintf(fdev, "%s", buf);
    }

    for (i = numwords; i < 7; i++)
        fprintf(fdev, "          ");

    fprintf(fdev, "\n");
}

void punch_object(const char *title)
{
    FILE *card_out;
    Address address;

    card_out = fopen("punch", "w+");
    if (NULL == card_out)
        error("Cannot open card puncher");
    for (address = 100; address < memory_size; address += 7) {
        punch_card(card_out, title, address);
    }
    fprintf(card_out, "TRANS0%04d", abs(entry_point));
    fflush(card_out);
    fclose(card_out);
}

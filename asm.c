/* MIX simulator, copyright 1994 by Darius Bacon */ 
#include "mix.h"

#include "asm.h"
#include "run.h"    /* for memory_fetch(), memory_store() */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

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
        char temp[16];
        unparse_cell(temp, cell, false);
        printf("%s(%u,%u): %s\n", address_to_string(address), L, R, temp);
    }
    memory_store(address, set_field(cell, make_field_spec(L, R), memory_fetch(address)));
}

void assemble(Cell cell)
{
    if (VERBOSE) {
        char temp[16];
        unparse_cell(temp, cell, true);
        printf("%s: %s  %s\n", address_to_string(here), temp, current_line);
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
    entry_point = address;
}

/* vim: set ts=4 sw=4 et: */

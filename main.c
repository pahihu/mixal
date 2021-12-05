/* MIX simulator, copyright 1994 by Darius Bacon */ 
#include "mix.h"

#include "asm.h"
#include "driver.h"
#include "parse.h"
#include "run.h"
#include "symbol.h"
#include "io.h"

#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *current_file = NULL;
static unsigned line_number;
static unsigned num_errors = 0;

void fatal_error(const char *message, ...)
{
    va_list args;

    if (current_file)   /*** of course, the error may not have anything to do with the file... */
        fprintf(stderr, "FATAL ERROR (%s, line %u): ", current_file, line_number);
    else
        fprintf(stderr, "FATAL ERROR: ");

    va_start(args, message);
    vfprintf(stderr, message, args);
    va_end(args);
    fprintf(stderr, "\n");

    io_shutdown();
    exit(1);
}

static void (*error_handler)(const char *, va_list) = NULL;

void install_error_handler(void (*handler)(const char *, va_list))
{
    error_handler = handler;
}

void error(const char *message, ...)
{
    va_list args;

    fflush(stdout);

    ++num_errors;
    va_start(args, message);
    if (error_handler)
        error_handler(message, args);
    else
        fatal_error("No error handler installed");
    va_end(args);
}

void warn(const char *message, ...)
{
    va_list args;

    if (current_file)
        fprintf(stderr, "WARNING (%s, line %u): ", current_file, line_number);
    else
        fprintf(stderr, "WARNING: ");
    va_start(args, message);
    vfprintf(stderr, message, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/* --- Miscellany --- */

static FILE *open_file(const char *filename, const char *mode)
{
    if (strcmp(filename, "-") == 0) {
        assert(mode[0] == 'r' || mode[0] == 'w');
        return mode[0] == 'w' ? stdout : stdin;
    } else {
        FILE *result = fopen(filename, mode);
        if (!result)
            fatal_error("%s: %s", filename, strerror(errno));
        return result;
    }
}

/* --- Main program --- */

static void assembler_error(const char *message, va_list args)
{
    if (current_file)
        fprintf(stderr, "ERROR (%s, line %u): ", current_file, line_number);
    else
        fprintf(stderr, "ERROR: ");
    vfprintf(stderr, message, args);
    fprintf(stderr, "\n");
}

static void assemble_file(const char *filename)
{
    FILE *infile = open_file(filename, "r");
    char line[257];

    current_file = filename, line_number = 0;
    while (fgets(line, sizeof line, infile)) {
        ++line_number;
        assemble_line(line);
        if (line[strlen(line) - 1] != '\n') {
            if (!fgets(line, sizeof line, infile))
                break;        /* the file's last line had no '\n' */
            else {
                error("Line length exceeds 256 characters");
                /* Skip the rest of the line */
                while (line[strlen(line) - 1] != '\n')
                    if (!fgets(line, sizeof line, infile))
                        break;
            }
        }
    }
    if (ferror(infile))
        error("%s: %s", filename, strerror(errno));
    fclose(infile);
    current_file = NULL;
}

static void usage()
{
    fprintf(stderr, "  usage: mixal [options...] [source...]\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "         -a             assemble only\n");
    fprintf(stderr, "         -b             binary MIX installed\n");
    fprintf(stderr, "         -c             core memory: mixcore.dat, mixcore.ctl\n");
    fprintf(stderr, "         -d             dump non-zero memory locations\n");
    fprintf(stderr, "         -e address     set entry point\n");
    fprintf(stderr, "         -f             floating-point attachment installed\n");
    fprintf(stderr, "         -g [devnum]    push GO button, [and set GO device]\n");
    fprintf(stderr, "         -h             print help\n");
    fprintf(stderr, "         -i             interrupt facility installed\n");
    fprintf(stderr, "         -m             Mixmaster installed\n");
    fprintf(stderr, "         -o             punch object deck\n");
    fprintf(stderr, "         -r             redirect reader/punch/printer\n");
    fprintf(stderr, "         -t n           trace instructions n times\n");
    fprintf(stderr, "         -x             double/indirect-indexing attachment installed\n");
    exit(1);
}

int main(int argc, char **argv)
{
    char title[8];
    Flag dump = false, punch = false, asm_only = false;
    
    precompute_field_data();

    /* Assemble the input: */
    strcpy(title, "OBJS");
    install_error_handler(assembler_error);
    if (argc == 1)
        assemble_file("-");
    else {
        int i, j;
        unsigned flags;

    /* --- Process flags --- */
        flags = 0;
        for (i = 1; i < argc; ++i) {
            if (*argv[i] == '-')
                switch (argv[i][1]) {
		case 'a': asm_only = true; break;
                case 'b': flags += MIXCONFIG_BINARY; break;
                case 'c': flags += MIXCONFIG_CORE; break;
                case 'd': dump = true; break;
                case 'e':
                    if (++i == argc) usage();
                    entry_point = atol(argv[i]);
                    break;
                case 'f': flags += MIXCONFIG_FLOAT; break;
                case 'g':
                    flags += MIXCONFIG_PUSHGO;
                    if (i+1 != argc) {
                        if (isdigit(*argv[i+1]))
                            io_set_go_device(atol(argv[++i]));
                    }
                    break;
                case 'h': usage(); break;
                case 'i': flags += MIXCONFIG_INTERRUPT; break;
                case 'm': flags += MIXCONFIG_MASTER; break;
                case 'o': punch = true; break;
                case 'r': io_redirect(); break;
                case 't':
                    if (++i == argc) usage();
                    set_trace_count(atol(argv[i]));
                    break;
                case 'x': flags += MIXCONFIG_INDEX; break;
                default: usage();
                }
        }
        set_configuration(flags);

        /* --- Assemble MIXAL source files --- */
        for (i = 1; i < argc; i++) {
            if (*argv[i] == '-') {
                char flag = argv[i][1];
                if (flag == 'e' || flag == 't')
                    i++;
                else if (flag == 'g') {
                    if ((i+1 != argc) && isdigit(*argv[i+1]))
                        i++;
                }
            } else {
                for (j = 0; j < 5; j++) {
                    int ch = argv[i][j];
                    if (ch == 0 || ch == '.')
                        break;
                    title[j] = toupper(ch);
                }
                title[j] = '\0';
                assemble_file(argv[i]);
            }
        }

    }
    resolve_generated_futures();
    if (num_errors != 0) {
        fprintf(stderr, "%u errors found\n", num_errors);
        exit(1);
    }

    /* --- Punch object deck --- */
    if (punch)
        punch_object_deck(title, entry_point);

    /* --- Check assemble only --- */
    if (asm_only)
        return 0;

    /* Now run it: */
    set_initial_state();
    {
        clock_t finish, start = clock();
        run();
        finish = clock();
        fprintf(stderr, "%g seconds elapsed\n", 
                (float)(finish - start) / CLOCKS_PER_SEC);
    }
    print_CPU_state();
    if (dump) print_DUMP();
    return 0;
}

/* MIX simulator, copyright 1994 by Darius Bacon */ 
#include "mix.h"
#include "charset.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>

static const char mix_chars[65] = 
  //           1         2         3         4         5
  // 01234567890123456789012345678901234567890123456789012345
    " ABCDEFGHI~JKLMNOPQR[#STUVWXYZ0123456789.,()+-*/=$<>@;:'???????";

#define num_chars (sizeof(mix_chars) - 1)

char mix_to_C_char(Byte mix_char)
{
    assert((unsigned)mix_char < num_chars);

    if (current_device_type == card_out) {
	/* --- Card punch cannot write characters 20, 21, 48, ... --- */
        if ((mix_char > 48) || (mix_char == 20) || (mix_char == 21))
            return ' ';
    }
    return mix_chars[(unsigned)mix_char];
}

Byte C_char_to_mix(char c)
{
    Byte ret;
    const char *s;

    s = strchr(mix_chars, toupper(c));
    if (!s) return 63;
    ret = (Byte) (s - mix_chars);

    /* --- Card reader cannot read, characters 20, 21, 48, ... --- */
    if (current_device_type == card_in) {
        if ((ret > 48) || (ret == 20) || (ret == 21))
            ret = 0;
    }
    return ret;
}

Byte C_int_to_mix(int c)
{
    if (c < 0 || c >= num_chars)
        error("Invalid MIX character code: %02o", c);
    return (Byte) c;
}

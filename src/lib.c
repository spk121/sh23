#include "lib.h"
#include <stdint.h>

static inline uint8_t ascii_tolower(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

/* ASCII-only case-insensitive string comparison (full strings) */
int ascii_strcasecmp(const char *s1, const char *s2)
{
    if (s1 == s2)
        return 0;

    while (*s1 && *s2)
    {
        uint8_t c1 = ascii_tolower((uint8_t)*s1++);
        uint8_t c2 = ascii_tolower((uint8_t)*s2++);

        if (c1 != c2)
        {
            return (int)c1 - (int)c2;
        }
    }

    /* One string ended; check the other */
    uint8_t c1 = ascii_tolower((uint8_t)*s1);
    uint8_t c2 = ascii_tolower((uint8_t)*s2);

    return (int)c1 - (int)c2;
}

/* ASCII-only case-insensitive string comparison (up to n bytes) */
int ascii_strncasecmp(const char *s1, const char *s2, int n)
{
    if (n == 0 || s1 == s2)
        return 0;

    while (n-- > 0 && *s1 && *s2)
    {
        uint8_t c1 = ascii_tolower((uint8_t)*s1++);
        uint8_t c2 = ascii_tolower((uint8_t)*s2++);
        if (c1 != c2)
        {
            return (int)c1 - (int)c2;
        }
    }

    if (n <= 0)
        return 0; // n exhausted without mismatch

    /* If n still > 0, compare next chars (or '\0' if end reached) */
    uint8_t c1 = ascii_tolower((uint8_t)*s1);
    uint8_t c2 = ascii_tolower((uint8_t)*s2);

    return (int)c1 - (int)c2;
}

#include "devutil.h"
#include <string.h>
#include <ctype.h>

bool dev_split(const char *dev, char *base_out, size_t base_cap, int *part_out) {
    if (part_out) *part_out = 0;
    if (!dev || strncmp(dev, "/dev/", 5) != 0) return false;

    size_t n = strlen(dev);
    if (n >= base_cap) return false;

    // find start of trailing digits
    size_t i = n;
    while (i > 0 && isdigit((unsigned char)dev[i-1])) i--;

    if (i < n) { // has digits
        int p = 0;
        for (size_t k = i; k < n; ++k) p = p*10 + (dev[k]-'0');
        if (part_out) *part_out = p;
        if (i >= base_cap) return false;
        memcpy(base_out, dev, i);
        base_out[i] = '\0';
        return true;
    }

    memcpy(base_out, dev, n+1);
    return true;
}

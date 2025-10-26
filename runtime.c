#include <stdint.h>

static uint64_t udivmod_u64(uint64_t num, uint64_t den, uint64_t *rem_out) {
    if (den == 0) {
        if (rem_out) {
            *rem_out = num;
        }
        return UINT64_MAX;
    }

    uint64_t quotient = 0;
    uint64_t remainder = 0;

    for (int bit = 63; bit >= 0; --bit) {
        remainder = (remainder << 1) | ((num >> bit) & 1ULL);
        if (remainder >= den) {
            remainder -= den;
            quotient |= (1ULL << bit);
        }
    }

    if (rem_out) {
        *rem_out = remainder;
    }

    return quotient;
}

uint64_t __udivdi3(uint64_t num, uint64_t den) {
    return udivmod_u64(num, den, 0);
}

uint64_t __umoddi3(uint64_t num, uint64_t den) {
    uint64_t remainder = 0;
    (void)udivmod_u64(num, den, &remainder);
    return remainder;
}

#pragma once
#include <stdbool.h>
#include <stdint.h>

/* swap first, then mirror in display coordinates; reject before subtracting. */
static inline bool chsc6x_transform(uint16_t *x, uint16_t *y, uint16_t width, uint16_t height,
                                    bool swap, bool mirror_x, bool mirror_y)
{
    if (!x || !y || !width || !height) return false;
    if (*x >= (swap ? height : width) || *y >= (swap ? width : height)) return false;
    if (swap) { uint16_t temp=*x; *x=*y; *y=temp; }
    if (mirror_x) *x=width-1-*x;
    if (mirror_y) *y=height-1-*y;
    return true;
}

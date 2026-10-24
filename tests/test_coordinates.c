/* Assertions must remain active even with a compiler's release-mode defaults. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "chsc6x_coordinates.h"

static void check_mapping(unsigned width, unsigned height)
{
    unsigned char seen[240 * 240];
    for (unsigned flags = 0; flags < 8; ++flags) {
        bool swap = flags & 1, mirror_x = flags & 2, mirror_y = flags & 4;
        memset(seen, 0, sizeof(seen));
        unsigned raw_width = swap ? height : width, raw_height = swap ? width : height;
        for (unsigned raw_y = 0; raw_y < raw_height; ++raw_y) {
            for (unsigned raw_x = 0; raw_x < raw_width; ++raw_x) {
                uint16_t x = raw_x, y = raw_y;
                assert(chsc6x_transform(&x, &y, width, height, swap, mirror_x, mirror_y));
                assert(x < width && y < height);
                assert(!seen[y * width + x]); /* No folded or duplicated coordinates. */
                seen[y * width + x] = 1;
            }
        }
        uint16_t x = raw_width, y = 0;
        assert(!chsc6x_transform(&x, &y, width, height, swap, mirror_x, mirror_y));
        x = 0; y = raw_height;
        assert(!chsc6x_transform(&x, &y, width, height, swap, mirror_x, mirror_y));
    }
}

int main(void)
{
    check_mapping(240, 240);
    check_mapping(120, 240);
    uint16_t x = 20, y = 30;
    assert(chsc6x_transform(&x, &y, 240, 240, true, true, false));
    assert(x == 209 && y == 20); /* Preserved mapping of the actual board. */
    assert(!chsc6x_transform(NULL, &y, 240, 240, false, false, false));
    assert(!chsc6x_transform(&x, &y, 0, 240, false, false, false));
    puts("PASS: all 8 transforms, full square/rectangular ranges, boundaries and board mapping");
    return 0;
}

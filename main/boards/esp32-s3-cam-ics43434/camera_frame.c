#include "camera_frame.h"

int camera_frame_center_offset(int panel_width, int frame_width)
{
    if (panel_width <= 0 || frame_width <= 0 || frame_width > panel_width) {
        return -1;
    }
    return (panel_width - frame_width) / 2;
}

void camera_frame_swap_bytes(uint16_t *dst, const uint16_t *src, size_t pixel_count)
{
    for (size_t i = 0; i < pixel_count; i++) {
        dst[i] = __builtin_bswap16(src[i]);
    }
}

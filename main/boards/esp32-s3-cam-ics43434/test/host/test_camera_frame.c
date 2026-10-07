/*
 * 主机侧单元测试 —— camera_frame.c 不依赖 ESP-IDF，能在 PC 上用 gcc 直接测。
 *
 * 这是摄像头预览里唯一能脱离硬件快速迭代的部分：字节序和居中算错了，
 * 在主机上就能发现，不用烧进去试。
 *
 * 编译运行：
 *   gcc -Wall -Wextra -o /tmp/t_camframe test/host/test_camera_frame.c \
 *       camera_frame.c -I. && /tmp/t_camframe
 *
 * ⚠️ 本文件放在 test/host/ 子目录里是有意的：板级目录的 *.c 会被
 *    main/CMakeLists.txt 的 file(GLOB ...) 收进固件（非递归），
 *    放子目录才能既被 gcc 编、又不进固件。
 */
#include <stdio.h>
#include <stdint.h>

#include "camera_frame.h"

static int g_fail = 0;

#define CHECK(cond) do { if (!(cond)) { \
    printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); g_fail++; } } while (0)

static void test_center_offset(void)
{
    /* 本板真实参数：屏 284 宽、画面 240 宽 → 两侧各留 22 */
    CHECK(camera_frame_center_offset(284, 240) == 22);
    /* 等宽 → 无黑边 */
    CHECK(camera_frame_center_offset(240, 240) == 0);
    /* 奇数差 → 向下取整 */
    CHECK(camera_frame_center_offset(241, 240) == 0);
    /* 装不下 → -1（调用方必须挡住这种情况） */
    CHECK(camera_frame_center_offset(240, 320) == -1);
    /* 非法参数 → -1 */
    CHECK(camera_frame_center_offset(284, 0) == -1);
    CHECK(camera_frame_center_offset(0, 240) == -1);
}

static void test_swap_bytes(void)
{
    uint16_t buf[3] = { 0x1234, 0x00FF, 0xABCD };

    camera_frame_swap_bytes(buf, buf, 3);          /* 原地交换必须合法 */
    CHECK(buf[0] == 0x3412);
    CHECK(buf[1] == 0xFF00);
    CHECK(buf[2] == 0xCDAB);

    /* 交换两次 = 还原（幂等性锚点） */
    camera_frame_swap_bytes(buf, buf, 3);
    CHECK(buf[0] == 0x1234);
    CHECK(buf[1] == 0x00FF);
    CHECK(buf[2] == 0xABCD);

    /* 异地交换：src 不能被改动 */
    uint16_t src[2] = { 0x1111, 0x2222 };
    uint16_t dst[2] = { 0, 0 };
    camera_frame_swap_bytes(dst, src, 2);
    CHECK(dst[0] == 0x1111 && dst[1] == 0x2222);
    CHECK(src[0] == 0x1111 && src[1] == 0x2222);
}

int main(void)
{
    printf("test_camera_frame\n");
    test_center_offset();
    test_swap_bytes();
    if (g_fail == 0) {
        printf("ALL PASS\n");
        return 0;
    }
    printf("%d FAILED\n", g_fail);
    return 1;
}

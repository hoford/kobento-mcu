// The STM32U5A9J-DK's hardware, for the player: a 480 x 480 round MIPI-DSI panel (LTDC
// through the GFXMMU), a Sitronix touch controller, the blue USER button, and the ST-LINK's
// virtual COM port (USART1, 115200) for the log.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_NAME "stm32u5a9j_dk"
#define BOARD_DISPLAY_NAME "STM32U5A9J-DK"
#define BOARD_WIDTH 480
#define BOARD_HEIGHT 480
#define BOARD_DENSITY 2.0f   // 240 x 240 dp, as the round Waveshare board
#define BOARD_ROUND 1

// Clocks, caches, the log port, the panel (on, full brightness) and touch. false: something
// didn't start (the log says which).
bool board_init(void);
// Show a BOARD_WIDTH x BOARD_HEIGHT ARGB8888 frame (rows packed). Copies it into the panel's
// frame buffer with the DMA2D and returns when the copy is done.
void board_present(const uint32_t* argb);
// The touch now: true and the point in panel pixels while a finger is down.
bool board_touch(int* x, int* y);
// The USER button was pressed since the last call.
bool board_button_pressed(void);
// Milliseconds and microseconds since start.
uint32_t board_ms(void);
uint64_t board_us(void);
// One character from the log port, or -1 if none waiting.
int board_getchar(void);
// Heap: bytes free (sbrk headroom + free blocks) and the most ever in use.
uint32_t board_heap_free(void);
// Set the wall clock (Unix seconds, UTC). Starts at the build time.
void board_set_time(int64_t unixSeconds);

#ifdef __cplusplus
}
#endif

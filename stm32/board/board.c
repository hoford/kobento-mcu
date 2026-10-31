// STM32U5A9J-DK board layer: see board.h. The panel set-up is ST's BSP (BSP_LCD_Init: DSI
// video mode, LTDC layer 0 in ARGB8888 on the GFXMMU's virtual buffer 0, whose lines are
// 768 pixels apart; the GFXMMU keeps only the visible circle in RAM).
#include "board.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "stm32u5xx_hal.h"
#include "stm32u5x9j_discovery.h"
#include "stm32u5x9j_discovery_lcd.h"
#include "stm32u5x9j_discovery_ts.h"
#include "stm32u5x9j_discovery_hspi.h"

#define VIRTUAL_LINE 768   // pixels per line in the GFXMMU virtual buffer (192 blocks x 16 B / 4)

extern GFXMMU_HandleTypeDef hlcd_gfxmmu;
extern LTDC_HandleTypeDef hlcd_ltdc;
extern DSI_HandleTypeDef hlcd_dsi;

// ── Interrupts ─────────────────────────────────────────────────────────────────────────

void SysTick_Handler(void) { HAL_IncTick(); }
void GFXMMU_IRQHandler(void) { HAL_GFXMMU_IRQHandler(&hlcd_gfxmmu); }
void LTDC_IRQHandler(void) { HAL_LTDC_IRQHandler(&hlcd_ltdc); }
void LTDC_ER_IRQHandler(void) { HAL_LTDC_IRQHandler(&hlcd_ltdc); }
void DSI_IRQHandler(void) { HAL_DSI_IRQHandler(&hlcd_dsi); }

// A fault: say where on the log port, then light the red LED and stop. The trampoline hands
// over the stack the exception frame was pushed on, before any compiler prologue moves sp.
void faultReport(uint32_t* frame) {
    printf("\r\nFAULT pc=%08lx lr=%08lx cfsr=%08lx hfsr=%08lx bfar=%08lx mmfar=%08lx\r\n",
           (unsigned long) frame[6], (unsigned long) frame[5], (unsigned long) SCB->CFSR,
           (unsigned long) SCB->HFSR, (unsigned long) SCB->BFAR, (unsigned long) SCB->MMFAR);
    // A rough backtrace: the return addresses (odd, in flash) on the stack above the frame.
    extern uint8_t _estack;
    printf("stack:");
    int n = 0;
    for (uint32_t* p = frame + 8; p < (uint32_t*) &_estack && n < 40; p++)
        if ((*p & 1) && *p >= 0x08000000 && *p < 0x08400000) { printf(" %08lx", (unsigned long) *p); n++; }
    printf("\r\n");
    BSP_LED_On(LED_RED);
    while (1) {}
}
__attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile("tst lr, #4\n"
                   "ite eq\n"
                   "mrseq r0, msp\n"
                   "mrsne r0, psp\n"
                   "b faultReport\n");
}
__attribute__((naked)) void MemManage_Handler(void) { __asm volatile("b HardFault_Handler"); }
__attribute__((naked)) void BusFault_Handler(void) { __asm volatile("b HardFault_Handler"); }
__attribute__((naked)) void UsageFault_Handler(void) { __asm volatile("b HardFault_Handler"); }

void Error_Handler(void) {
    printf("Error_Handler\r\n");
    BSP_LED_On(LED_RED);
    while (1) {}
}

// ── Clocks ─────────────────────────────────────────────────────────────────────────────

// 160 MHz from the PLL on MSI, as ST's examples for this board.
static bool clockInit(void) {
    RCC_ClkInitTypeDef clk = {0};
    RCC_OscInitTypeDef osc = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);
    HAL_PWREx_ConfigSupply(PWR_SMPS_SUPPLY);
    __HAL_RCC_PWR_CLK_DISABLE();
    osc.OscillatorType = RCC_OSCILLATORTYPE_MSI | RCC_OSCILLATORTYPE_HSE;
    osc.MSIState = RCC_MSI_ON;
    osc.HSEState = RCC_HSE_ON;
    osc.MSIClockRange = RCC_MSIRANGE_4;
    osc.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    osc.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
    osc.PLL.PLLM = 1;
    osc.PLL.PLLN = 80;
    osc.PLL.PLLR = 2;
    osc.PLL.PLLP = 2;
    osc.PLL.PLLQ = 2;
    osc.PLL.PLLFRACN = 0;
    osc.PLL.PLLRGE = RCC_PLLVCIRANGE_0;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) return false;
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 |
                    RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK3;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    clk.APB3CLKDivider = RCC_HCLK_DIV1;
    return HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) == HAL_OK;
}

// ── Board API ──────────────────────────────────────────────────────────────────────────

// ── PSRAM: the heap ────────────────────────────────────────────────────────────────────

// 64 MB of HSPI PSRAM (APS512XX), memory-mapped at 0xA0000000. The default memory map
// makes 0xA0000000 device memory (uncached, and unaligned accesses fault), so an MPU region
// marks it normal, write-back cached RAM, and DCACHE1 caches it.
#define PSRAM_BASE 0xA0000000u
#define PSRAM_SIZE 0x4000000u
static uint8_t* sHeapTop;   // the next byte _sbrk hands out
static uint8_t* sHeapEnd;   // where the current region ends
static bool sPsram;
static DCACHE_HandleTypeDef sDcache;

static bool psramInit(void) {
    BSP_HSPI_RAM_Cfg_t cfg = {0};
    cfg.LatencyType = BSP_HSPI_RAM_VARIABLE_LATENCY;
    cfg.BurstType = BSP_HSPI_RAM_LINEAR_BURST;
    cfg.BurstLength = BSP_HSPI_RAM_BURST_64_BYTES;
    cfg.ReadLatencyCode = BSP_HSPI_RAM_READ_LATENCY_6;
    cfg.WriteLatencyCode = BSP_HSPI_RAM_WRITE_LATENCY_6;
    cfg.IOMode = BSP_HSPI_RAM_IO_X16_MODE;
    if (BSP_HSPI_RAM_Init(0, &cfg) != BSP_ERROR_NONE) { printf("PSRAM init failed\r\n"); return false; }
    if (BSP_HSPI_RAM_EnableMemoryMappedMode(0) != BSP_ERROR_NONE) { printf("PSRAM mapping failed\r\n"); return false; }

    HAL_MPU_Disable();
    MPU_Attributes_InitTypeDef attr = {0};
    attr.Number = MPU_ATTRIBUTES_NUMBER0;
    attr.Attributes = INNER_OUTER(MPU_WRITE_BACK | MPU_NON_TRANSIENT | MPU_RW_ALLOCATE);
    HAL_MPU_ConfigMemoryAttributes(&attr);
    MPU_Region_InitTypeDef r = {0};
    r.Enable = MPU_REGION_ENABLE;
    r.Number = MPU_REGION_NUMBER0;
    r.BaseAddress = PSRAM_BASE;
    r.LimitAddress = PSRAM_BASE + PSRAM_SIZE - 1;
    r.AttributesIndex = MPU_ATTRIBUTES_NUMBER0;
    r.AccessPermission = MPU_REGION_ALL_RW;
    r.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    r.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    HAL_MPU_ConfigRegion(&r);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

    sDcache.Instance = DCACHE1;
    sDcache.Init.ReadBurstType = DCACHE_READ_BURST_WRAP;
    if (HAL_DCACHE_Init(&sDcache) != HAL_OK || HAL_DCACHE_Enable(&sDcache) != HAL_OK) printf("DCACHE failed\r\n");

    // A quick check before the heap moves there.
    volatile uint32_t* p = (volatile uint32_t*) PSRAM_BASE;
    for (uint32_t i = 0; i < 4096; i++) p[i * 4097 % (PSRAM_SIZE / 4)] = i * 2654435761u;
    for (uint32_t i = 0; i < 4096; i++)
        if (p[i * 4097 % (PSRAM_SIZE / 4)] != i * 2654435761u) { printf("PSRAM check failed at %lu\r\n", (unsigned long) i); return false; }
    sHeapTop = (uint8_t*) PSRAM_BASE;
    sHeapEnd = (uint8_t*) PSRAM_BASE + PSRAM_SIZE;
    sPsram = true;
    printf("PSRAM: %u MB, the heap from here on\r\n", (unsigned) (PSRAM_SIZE >> 20));
    return true;
}

// newlib's malloc grows through this. Until the PSRAM is up (constructors before main), the
// heap is the on-chip RAM between .bss and the stack; after, the PSRAM. newlib-nano's malloc
// takes whatever sbrk returns, so the jump between regions is fine.
void* _sbrk(ptrdiff_t incr) {
    extern uint8_t _end, _estack;
    extern uint32_t _Min_Stack_Size;
    if (!sHeapTop) {
        sHeapTop = &_end;
        sHeapEnd = &_estack - (uint32_t) &_Min_Stack_Size;
    }
    if (sHeapTop + incr > sHeapEnd) {
        errno = ENOMEM;
        return (void*) -1;
    }
    uint8_t* prev = sHeapTop;
    sHeapTop += incr;
    return prev;
}

static int64_t sEpochAtStartMs;
static void consoleRxInit(void);   // wall clock = this + board_ms()

bool board_init(void) {
    HAL_Init();
    if (!clockInit()) return false;
    // The instruction cache: code runs from flash, which is far slower without it.
    HAL_ICACHE_ConfigAssociativityMode(ICACHE_2WAYS);
    HAL_ICACHE_Enable();
    // DWT cycle counter, for board_us().
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    // Fault handlers of their own rather than all as HardFault.
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;

    COM_InitTypeDef com = {115200, COM_WORDLENGTH_8B, COM_STOPBITS_1, COM_PARITY_NONE, COM_HWCONTROL_NONE};
    BSP_COM_Init(COM1, &com);
    setvbuf(stdout, NULL, _IONBF, 0);
    consoleRxInit();
    printf("\r\n%s: %lu MHz\r\n", BOARD_DISPLAY_NAME, (unsigned long) (HAL_RCC_GetSysClockFreq() / 1000000));

    BSP_LED_Init(LED_RED);
    BSP_LED_Init(LED_GREEN);
    BSP_PB_Init(BUTTON_USER, BUTTON_MODE_GPIO);

    psramInit();

    if (BSP_LCD_Init(0, LCD_ORIENTATION_PORTRAIT) != BSP_ERROR_NONE) {
        printf("LCD init failed\r\n");
        return false;
    }
    BSP_LCD_SetBrightness(0, 100);
    BSP_LCD_DisplayOn(0);

    TS_Init_t ts = {BOARD_WIDTH, BOARD_HEIGHT, TS_ORIENTATION_PORTRAIT, 0};
    if (BSP_TS_Init(0, &ts) != BSP_ERROR_NONE) printf("touch init failed\r\n");

    sEpochAtStartMs = (int64_t) BUILD_EPOCH * 1000;
    return true;
}

void board_present(const uint32_t* argb) {
    __HAL_RCC_DMA2D_CLK_ENABLE();
    // Memory to memory, ARGB8888 in and out: the packed frame into the virtual buffer.
    DMA2D->CR = 0;   // mode 0: memory to memory
    DMA2D->FGMAR = (uint32_t) argb;
    DMA2D->FGOR = 0;
    DMA2D->FGPFCCR = 0;   // ARGB8888
    DMA2D->OMAR = GFXMMU_VIRTUAL_BUFFER0_BASE;
    DMA2D->OOR = VIRTUAL_LINE - BOARD_WIDTH;
    DMA2D->OPFCCR = 0;    // ARGB8888
    DMA2D->NLR = ((uint32_t) BOARD_WIDTH << DMA2D_NLR_PL_Pos) | BOARD_HEIGHT;
    DMA2D->CR |= DMA2D_CR_START;
    while (DMA2D->CR & DMA2D_CR_START) {}
}

bool board_touch(int* x, int* y) {
    TS_State_t s = {0};
    if (BSP_TS_GetState(0, &s) != BSP_ERROR_NONE || !s.TouchDetected) return false;
    *x = (int) s.TouchX;
    *y = (int) s.TouchY;
    return true;
}

bool board_button_pressed(void) {
    static int was = 0;
    int now = BSP_PB_GetState(BUTTON_USER);
    bool pressed = now && !was;
    was = now;
    return pressed;
}

uint32_t board_ms(void) { return HAL_GetTick(); }

// The cycle counter wraps every 26.8 s at 160 MHz: extended to 64 bits here, so call it at
// least that often (the player loop does).
uint64_t board_us(void) {
    static uint64_t high = 0;
    static uint32_t last = 0;
    uint32_t c = DWT->CYCCNT;
    if (c < last) high += 1ULL << 32;
    last = c;
    return (high | c) / (SystemCoreClock / 1000000);
}

// Console input arrives while a frame renders (up to 200 ms): the USART holds one byte, so
// its interrupt moves bytes into this ring as they come.
static volatile uint8_t sRx[256];
static volatile uint8_t sRxHead, sRxTail;

void USART1_IRQHandler(void) {
    USART_TypeDef* u = USART1;
    if (u->ISR & USART_ISR_ORE) u->ICR = USART_ICR_ORECF;
    while (u->ISR & USART_ISR_RXNE_RXFNE) {
        uint8_t c = (uint8_t) u->RDR;
        uint8_t next = (uint8_t) (sRxHead + 1);
        if (next != sRxTail) { sRx[sRxHead] = c; sRxHead = next; }
    }
}

static void consoleRxInit(void) {
    USART1->CR1 |= USART_CR1_RXNEIE_RXFNEIE;
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

int board_getchar(void) {
    if (sRxTail == sRxHead) return -1;
    int c = sRx[sRxTail];
    sRxTail = (uint8_t) (sRxTail + 1);
    return c;
}

void board_set_time(int64_t unixSeconds) { sEpochAtStartMs = unixSeconds * 1000 - HAL_GetTick(); }

// ── C library hooks ────────────────────────────────────────────────────────────────────

extern int __io_putchar(int ch);   // the BSP's: one character to COM1

int _write(int fd, const char* p, int n) {
    (void) fd;
    for (int i = 0; i < n; i++) __io_putchar(p[i]);
    return n;
}

int _gettimeofday(struct timeval* tv, void* tz) {
    (void) tz;
    int64_t ms = sEpochAtStartMs + HAL_GetTick();
    tv->tv_sec = (time_t) (ms / 1000);
    tv->tv_usec = (suseconds_t) ((ms % 1000) * 1000);
    return 0;
}

#include <malloc.h>
#include <unistd.h>

uint32_t board_heap_free(void) {
    struct mallinfo m = mallinfo();
    return (uint32_t) (sHeapEnd - sHeapTop) + (uint32_t) m.fordblks;
}

/* Linker script for the STM32U5A9NJ: rcplayer runs from internal flash, and everything it
   doesn't place in PSRAM goes in the on-chip SRAM. Stack at the top of SRAM; _sbrk in board.c
   hands out the rest, or PSRAM. */

ENTRY(Reset_Handler)

MEMORY
{
    RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 2496K
    ROM (rx)  : ORIGIN = 0x08000000, LENGTH = 4096K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size = 0x10000;    /* what the link keeps free, checked in ._user_heap_stack */
_Min_Stack_Size = 0x10000;

SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } >ROM

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } >ROM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >ROM

    /* C++ unwinding tables. */
    .ARM.extab (READONLY) : { . = ALIGN(4); *(.ARM.extab* .gnu.linkonce.armextab.*) . = ALIGN(4); } >ROM
    .ARM (READONLY) :
    {
        . = ALIGN(4);
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
        . = ALIGN(4);
    } >ROM

    /* Static constructors and destructors, walked by __libc_init_array. */
    .preinit_array (READONLY) :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
        . = ALIGN(4);
    } >ROM
    .init_array (READONLY) :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
        . = ALIGN(4);
    } >ROM
    .fini_array (READONLY) :
    {
        . = ALIGN(4);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
        . = ALIGN(4);
    } >ROM

    _sidata = LOADADDR(.data);   /* the startup code copies .data from here */

    /* Thread-local variables (rccore has a few). One thread, so one copy: runtime.c builds
       it from this template at the first access (__aeabi_read_tp). */
    .tdata :
    {
        . = ALIGN(8);
        __tdata_start = .;
        *(.tdata .tdata.* .gnu.linkonce.td.*)
        . = ALIGN(8);
        __tdata_end = .;
    } >ROM
    .tbss :
    {
        __tbss_start = .;
        *(.tbss .tbss.* .gnu.linkonce.tb.*)
        *(.tcommon)
        . = ALIGN(8);
        __tbss_end = .;
    } >ROM

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        *(.RamFunc)
        *(.RamFunc*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> ROM

    . = ALIGN(4);
    .bss :
    {
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM

    /* Fails the link if SRAM can't hold the minimum heap and stack. */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size + _Min_Stack_Size;
        . = ALIGN(8);
    } >RAM

    /DISCARD/ : { libc.a(*) libm.a(*) libgcc.a(*) }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}

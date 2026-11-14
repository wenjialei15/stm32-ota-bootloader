/*
 * 应用链接脚本。同一份源码编两份，用 --defsym=SLOT_BASE=... 指定槽地址。
 *
 * 为什么必须两份：Cortex-M 的向量表项、字面量池里的函数地址、跳转指令
 * 全都是绝对地址，链接到 0x08003000 的固件放到 0x08009000 会全线跑飞。
 * 要一份固件跑两个槽，得做位置无关代码（PIC），那是另一个量级的工作量。
 */

_MY_SLOT_BASE = DEFINED(SLOT_BASE) ? SLOT_BASE : 0x08003000;

/* 镜像头占 32 字节，代码从它后面开始 */
_image_base = _MY_SLOT_BASE + 32;

ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = _image_base, LENGTH = 24544
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT > FLASH

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM
}

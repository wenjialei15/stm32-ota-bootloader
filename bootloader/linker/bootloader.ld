/*
 * Bootloader 链接脚本：0x08000000 起 8 KB。
 * 这段空间在任何情况下都不允许被擦除，否则设备变砖。
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 8K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

/* 栈顶放在 RAM 末尾，向下生长 */
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  /* 向量表必须落在镜像最开头：复位时硬件从 0x08000000 取 MSP、从 +4 取入口 */
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

  /* .data 的初值存在 Flash 里，Reset_Handler 负责搬到 RAM */
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

  /* 链接期就能拦下的越界：如果 Bootloader 超过 8 KB 直接报错，
     而不是等到运行时把元数据区踩了才发现 */
  ASSERT(SIZEOF(.text) + SIZEOF(.isr_vector) <= LENGTH(FLASH),
         "Bootloader exceeds 8 KB flash budget")
}

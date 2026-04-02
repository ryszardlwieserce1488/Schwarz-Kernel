ENTRY(_start)

SECTIONS
{
  . = 0x100000;

  .text : ALIGN(16)
  {
    KEEP(*(.text.start))
    *(.text*)
  }

  .rodata : ALIGN(16)
  {
    *(.rodata*)
    *(.rdata*)
  }

  .data : ALIGN(16)
  {
    *(.data*)
  }

  .kernel_stack : ALIGN(16)
  {
    *(.kernel_stack)
  }
  
  .bss : ALIGN(16)
  {
    *(.bss*)
    *(COMMON)
  }

  /DISCARD/ :
  {
    *(.eh_frame*)
    *(.comment*)
    *(.note*)
    *(.llvm_addrsig)
  }
}

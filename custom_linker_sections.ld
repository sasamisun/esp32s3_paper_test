/* PSRAM配置 */
SECTIONS
{
  psram_segment (NOLOAD) : ALIGN(4)
  {
      . = ALIGN(4);
      *(.rodata.font*)      /* フォントデータをPSRAMに配置 */
      *(.ext_ram.bss*)      /* PSRAM用BSS領域 */
      . = ALIGN(4);
  } > external_ram
}
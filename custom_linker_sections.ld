/* custom_linker_sections.ld */
SECTIONS
{
  /* アプリケーション記述子セクションとROデータセクションを明示的に連続化 */
  .flash.appdesc :
  {
    *(.flash.appdesc)
  } > default_rodata_seg

  .flash.rodata :
  {
    *(.rodata)
    *(.rodata.*)
    *(.irom.text) /* irom0のテキスト領域 */
    *(.gnu.linkonce.r.*)
    *(.rodata1)
    __rodata_end = ABSOLUTE(.);
  } > default_rodata_seg
}
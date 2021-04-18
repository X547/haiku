.global gFixedFont
.type   gFixedFont, @object
.data
gFixedFont:
.incbin "/boot/data/packages/haiku/src/system/boot/platform/riscv/fixed_font.bin"
.size	gFixedFont, .-gFixedFont

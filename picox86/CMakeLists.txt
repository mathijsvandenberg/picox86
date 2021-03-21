# Replace TMDS with 10 bit UART (same baud rate):
# add_definitions(-DDVI_SERIAL_DEBUG=1)
# add_definitions(-DRUN_FROM_CRYSTAL)

add_executable(picox86
	main.c
	spiflash.c
	cpux86.c
	tmds_encode_font_2bpp.S
	tmds_encode_font_2bpp.h
)

# TODO this should work ok with DVI_N_TMDS_BUFFERS=2 (perhaps need to
# rearrange some pushes/pops) and also as we are monochrome the buffers are 3x
# as big as they need to be 
#target_compile_definitions(picox86 PRIVATE DVI_VERTICAL_REPEAT=1 DVI_N_TMDS_BUFFERS=3 DVI_MONOCHROME_TMDS)
target_compile_definitions(picox86 PRIVATE DVI_VERTICAL_REPEAT=1)

# We have a lot in SRAM4 (particularly TMDS LUT) but don't need much stack on
# core 1. Probably even 256 bytes would be fine.
target_compile_definitions(picox86 PRIVATE PICO_CORE1_STACK_SIZE=0x200)



target_link_libraries(picox86
	pico_stdlib
	hardware_adc
	hardware_spi
	pico_multicore
	#pico_util
	libdvi
)

# create map/bin/hex file etc.
pico_add_extra_outputs(picox86)

DEVICE  = atmega48
F_CPU   = 12000000	# in Hz
FUSE_L  = 0xdf # see below for fuse values for particular devices
FUSE_H  = 0xdd
AVRDUDE = avrdude -c avrispmkII -P usb -p $(DEVICE) # edit this line for your programmer
OUT     = 1bitsynth

SRC     = 1bitsynth.c
CHSRC   = 

COMPILE = avr-gcc -Wall -O3 -DF_CPU=$(F_CPU) $(CFLAGS) -mmcu=$(DEVICE)
OBJECTS = $(SRC:.c=.o) $(CHSRC:.ch=.o)

# symbolic targets:
help:
	@echo "This Makefile has no default rule. Use one of the following:"
	@echo "make hex ....... to build hex file"
	@echo "make program ... to flash fuses and firmware"
	@echo "make fuse ...... to flash the fuses"
	@echo "make flash ..... to flash the firmware (use this on metaboard)"
	@echo "make clean ..... to delete objects and hex file"

hex: $(OUT).hex

program: flash fuse

# rule for programming fuse bits:
fuse:
	@[ "$(FUSE_H)" != "" -a "$(FUSE_L)" != "" ] || \
		{ echo "*** Edit Makefile and choose values for FUSE_L and FUSE_H!"; exit 1; }
	$(AVRDUDE) -U hfuse:w:$(FUSE_H):m -U lfuse:w:$(FUSE_L):m

# rule for uploading firmware:
flash: $(OUT).hex
	$(AVRDUDE) -U flash:w:$(OUT).hex:i

# rule for deleting dependent files (those which can be built by Make):
clean:
	rm -f $(OUT).hex $(OUT).lst $(OUT).obj $(OUT).cof $(OUT).list $(OUT).map $(OUT).eep.hex $(OUT).elf *.o

# Generic rule for compiling C files:
.c.o:
	$(COMPILE) -c $< -o $@

# Generic rule for compiling combined C and header files:
.ch.o:
	$(COMPILE) $(CHFLAGS) -c $< -o $@

# Generic rule for assembling Assembler source files:
.S.o:
	$(COMPILE) -x assembler-with-cpp -c $< -o $@
# "-x assembler-with-cpp" should not be necessary since this is the default
# file type for the .S (with capital S) extension. However, upper case
# characters are not always preserved on Windows. To ensure WinAVR
# compatibility define the file type manually.

# Generic rule for compiling C to assembler, used for debugging only.
.c.s:
	$(COMPILE) -S $< -o $@

# file targets:

$(OUT).elf: $(OBJECTS)
	$(COMPILE) -o $(OUT).elf $(OBJECTS)

$(OUT).hex: $(OUT).elf
	rm -f $(OUT).hex $(OUT).eep.hex
	avr-objcopy -j .text -j .data -O ihex $(OUT).elf $(OUT).hex
	avr-size $(OUT).hex

# debugging targets:

disasm:	$(OUT).elf
	avr-objdump -d $(OUT).elf

cpp:
	$(COMPILE) -E $(SRC)

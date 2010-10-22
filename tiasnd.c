// Atari TIA sound generator emulation routines
// Algorithms adapted from the Stella source code (stella.sourceforge.net)
// Matt Sarnoff (www.msarnoff.org)
// October 20, 2010

#import <stdio.h>
#import <stdint.h>

// 4-bit and 5-bit linear feedback shift registers
static uint8_t sr4 = 1;
static uint8_t sr5 = 1;

// Clock the 4-bit register
static inline void shift4()
{
  // 4-bit linear feedback shift register, taps at bits 3 and 2
  sr4 = ((sr4 << 1) | (!!(sr4 & 0x08) ^ !!(sr4 & 0x04))) & 0x0F;
}

// Clock the 5-bit register
static inline void shift5()
{
  // 5-bit linear feedback shift register, taps at bits 4 and 2
  sr5 = ((sr5 << 1) | (!!(sr5 & 0x10) ^ !!(sr5 & 0x04))) & 0x1F;
}

// Generates the bit pattern 01010101...
static inline void div4_two()
{
  sr4 = ((sr4 << 1) | (!(sr4 & 0x01))) & 0x0F;
}

// Generates the bit pattern 000111000111...
static inline void div4_six()
{
  sr4 = (~sr4 << 1) | (!(!(!(sr4 & 0x04) && ((sr4 & 0x07)))));
}

// TIA waveform 1
static void wave_poly4()
{
  shift4();
}

// TIA waveform 2
static void wave_div31poly4()
{
  shift5();
  // 5-bit register clocks 4-bit register
  if ((sr5 & 0x0F) == 0x08)
    shift4();
}

// TIA waveform 3
static void wave_poly5poly4()
{
  shift5();
  // 5-bit register clocks 4-bit register
  if (sr5 & 0x10)
    shift4();
}

// TIA waveforms 4 and 5
static void wave_div2()
{
  div4_two();
}

// TIA waveform 6
static void wave_div31div2()
{
  shift5();
  if ((sr5 & 0x0F) == 0x08)
    div4_two();
}

// TIA waveform 7
static void wave_poly5div2()
{
  shift5();
  if (sr5 & 0x10)
    div4_two();
}

// TIA waveform 8
static void wave_poly9()
{
  // taps at bits 8 and 4
  sr5 = (sr5 << 1) | (!!(sr4 & 0x08) ^ !!(sr5 & 0x10));
  sr4 = ((sr4 << 1) | (!!(sr5 & 0x20))) & 0x0F;
  sr5 &= 0x1F;
}

// TIA waveform 9
static void wave_poly5()
{
  sr5 = (sr5 << 1) | (!!(sr5 & 0x10) ^ !!(sr5 & 0x04));
  sr4 = ((sr4 << 1) | (!!(sr5 & 0x20))) & 0x0F; 
  sr5 &= 0x1F;  
}

// TIA waveform A
static void wave_div31()
{
  shift5();
  if ((sr5 & 0x0F) == 0x08)
  sr4 = ((sr4 << 1) | (!!(sr5 & 0x10))) & 0x0F; 
}

// TIA waveforms C and D
static void wave_div6()
{
  div4_six();
}

// TIA waveform E
static void wave_div31div6()
{
  shift5();
  if ((sr5 & 0x0F) == 0x08)
    div4_six();
}

// TIA waveform F
static void wave_poly5div6()
{
  shift5();
  if (sr5 & 0x10)
    div4_six();
}

typedef void (*wavefnptr)();
static wavefnptr wavefns[8] = {
  wave_poly4,
  wave_poly5poly4,
  wave_div2,
  wave_div31div2,
  wave_poly5div2,
  wave_poly9,
  wave_poly5,
  wave_poly5div6,
};

// Generate and return one sample of the specified waveform
// Sample value is 0 or 1.
uint8_t tia_out(uint8_t waveformnum)
{
  wavefns[waveformnum & 0x07]();
  return !!(sr4 & 0x08);
}

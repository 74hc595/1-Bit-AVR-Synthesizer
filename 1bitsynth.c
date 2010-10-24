/* One-bit synthesizer
 * Matt Sarnoff (www.msarnoff.org)
 *
 * Requires 12MHz clock frequency
 * High fuse byte: 0xDD
 * Low fuse byte:  0xDF
 *
 * Oct 27, 2009 - original
 * Dec 12, 2009 - changed knob layout
 * Dec 15, 2009 - added MIDI IN support
 * Oct 19, 2010 - replaced waveform switches with pots
 */

#include <avr/sfr_defs.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdlib.h>

#define set_bit(v,b)        v |= _BV(b)
#define clear_bit(v,b)      v &= ~_BV(b)
#define pulse_bit(v,b)      do { set_bit(v,b); clear_bit(v,b); } while(0)
#define pulse_bit_low(v,b)  do { clear_bit(v,b); set_bit(v,b); } while(0)

#define PASTE(x,y)  x ## y
#define PORT(x)     PASTE(PORT,x)
#define PIN(x)      PASTE(PIN,x)
#define DDR(x)      PASTE(DDR,x)

#define AUDIO       B
#define AUDIO_PIN   0

#define LEDS        B
#define LFO_LED_PIN 2
#define ENV_LED_PIN 3
#define PWR_LED_PIN 4

#define MUXCTL      D
#define MUXCTL_PIN  5

#define NUM_KNOBS         9
#define NUM_ADC_SAMPLES   32
#define NUM_ADC_CHANNELS  6
#define MUXED_CH_LOW      3
#define MUXED_CH_HIGH     5

#define PITCH_KNOB        4
#define LFO_FREQ_KNOB     3//6
#define LFO_DEPTH_KNOB    2//1
#define ENV_FREQ_KNOB     6//3
#define ENV_WIDTH_KNOB    1//2
#define AUDIO_WF_KNOB     7
#define LFO_WF_KNOB       0
#define EXT_KNOB_1        5
#define EXT_KNOB_2        8

// 16-bit timer output compare values corresponding to MIDI note frequencies
// Computed for 12MHz CPU and 1/8 prescaler
uint16_t freqvals[128] PROGMEM =
{
  65535,65535,65535,65535,65535,65535,64865,61224,
  57788,54544,51483,48593,45866,43292,40862,38568,
  36404,34360,32432,30612,28893,27272,25741,24296,
  22933,21645,20430,19284,18201,17180,16215,15305,
  14446,13635,12870,12148,11466,10822,10215,9641,
  9100,8589,8107,7652,7223,6817,6435,6073,
  5732,5411,5107,4820,4550,4294,4053,3826,
  3611,3408,3217,3036,2866,2705,2553,2410,
  2274,2147,2026,1912,1805,1704,1608,1518,
  1432,1352,1276,1204,1137,1073,1013,956,
  902,851,803,758,716,675,637,602,
  568,536,506,477,450,425,401,379,
  357,337,318,300,283,267,252,238,
  225,212,200,189,178,168,159,150,
  141,133,126,119,112,106,100,94,
  89,84,79,74,70,66,62,59
};

typedef struct
{
  uint16_t val;
  uint32_t accum;
} knob_t; 

static uint16_t waveforms[9] =
{
  0b1100000000000000,
  0b1111111100000000,
  0b1110111111101111,
  0b1011011011011011,
  0b0010100001110110,
  0b1010101011010101,
  0b1010101010101010,
  0b0000000000000000, // noise
  0b0000000000000000  // dummy
};

volatile uint8_t adctimer = 0;
knob_t knobs[NUM_KNOBS] = {{0}};

// parameters
uint16_t pitch = 400;   // set pitch
uint16_t lfofreq = 0;   // lfo period
uint16_t lfodepth = 0;  // lfo wave amplitude
uint8_t envfreq = 0;    // envelope period
uint8_t envwidth = 0;   // envelope duty cycle
volatile uint8_t wavenum = 0; // oscillator waveform
uint8_t lfowavenum = 8; // lfo waveform

// midi state
enum { MIDI_OTHER, MIDI_NOTE_OFF, MIDI_NOTE_ON };
volatile uint8_t usemidi = 0;  // disables pitch knob
volatile uint8_t noteon = 1;
uint8_t midimode = MIDI_OTHER;
int8_t midibytesleft = 0;
uint8_t midilastnote = 0xFF;
uint8_t midibuffer[2];

// state
uint16_t outpitch = 0;  // OCR1A value
uint8_t lfotimer = 0;   // lfo value, counts from 0 to lfofreq
uint16_t lfoval = 0;    // amount to add to output pitch
uint16_t lfodelta = 0;
uint8_t envtimer = 0;   // envelope value, counts from 0 to envfreq
volatile uint8_t envval = 1; // high or low; ANDed with output wave
volatile uint16_t waveform;

uint8_t ledpulse = 0;   // turn off the power LED when nonzero

// faster noise generator than rand()
uint16_t lfsr_rand()
{
  static uint16_t lfsr = 0xACE1u;
  lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB400u);
  return lfsr;
}

void midi_init()
{
  // set baud rate
#define BAUD 31250
#include <util/setbaud.h>
  UBRR0H = UBRRH_VALUE;
  UBRR0L = UBRRL_VALUE;
#if USE_2X
  UCSR0A |= _BV(U2X0);
#else
  UCSR0A &= ~_BV(U2X0);
#endif

  // enable rx, turn on interrupt
  // 8 data bits, no parity, 1 stop bit
  UCSR0B = _BV(RXCIE0) | _BV(RXEN0);
  UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
}

// midi receive interrupt
ISR(USART_RX_vect)
{
  // get the new byte
  uint8_t byte = UDR0;
  
  // is it a status byte?
  if (byte >= 0x80)
  {
    usemidi = 1;
    switch (byte)
    {
      case 0x80:
        midimode = MIDI_NOTE_OFF;
        midibytesleft = 2;
        break;
      case 0x90:
        midimode = MIDI_NOTE_ON;
        midibytesleft = 2;
        break;
      case 0xFE: // active sense
        break;
      case 0xFC: // stop
      case 0xFF: // reset
        noteon = 0;
      default:
        midimode = MIDI_OTHER;
        midibytesleft = 0;
    }
  }
  else
  {
    // save the second and third bytes
    if (midibytesleft > 0)
    {
      midibuffer[0] = midibuffer[1];
      midibuffer[1] = byte;
      midibytesleft--;
    }

    // got all bytes; parse message
    if (midibytesleft <= 0)
    {
      uint8_t note = midibuffer[0];
      switch (midimode)
      {
        case MIDI_NOTE_OFF:
          if (note == midilastnote)
          {
            noteon = 0;
            midilastnote = 0xFF;
          }
          midibytesleft = 2; // receive more note off events
          break;
        case MIDI_NOTE_ON:
          // turn note off if velocity is zero
          if (midibuffer[1] == 0 && note == midilastnote)
          {
            noteon = 0;
            midilastnote = 0xFF;
          }
          else if (midibuffer[1] != 0)
          {
            noteon = 1;
            pitch = pgm_read_word(freqvals+note);
            midilastnote = note;
          }
          midibytesleft = 2; // receive more note on events
          break;
        default:
          break;
      }
    }
  }
}

// timer interrupt; update audio out
ISR(TIMER1_COMPA_vect)
{
  uint8_t shiftout;
  if (wavenum != 7)
  {
    shiftout = waveform & 1;
    waveform >>= 1;
    if (shiftout)
      waveform |= (1 << 15);
  }
  else
  {
    shiftout = lfsr_rand() & 1;
  }

  // apply envelope
  shiftout &= envval;
  
  // mute if note off
  shiftout &= noteon;

  // update audio pin
  if (shiftout)
    set_bit(PORT(AUDIO), AUDIO_PIN);
  else
    clear_bit(PORT(AUDIO), AUDIO_PIN);
}

// sample an ADC channel
void read_adc(knob_t *k)
{
  uint16_t val = ADCL;
  val += (ADCH << 8);

  // accumulate value
  k->accum += val;

  // take average when timer overflows
  if (adctimer == 0)
  {
    k->val = k->accum / NUM_ADC_SAMPLES;
    k->accum = 0;
  }
}

// set new pitch
void update_pitch()
{
  uint16_t newpitch = (usemidi) ? pitch-(lfoval*4U)
                                : 200U + ((pitch-lfoval)*4U);
  if (newpitch != outpitch)
  {
    outpitch = newpitch;
    if (TCNT1 > outpitch)
      TCNT1 = 0;
    OCR1A = outpitch;
  }
}

// update lfo state
void update_lfo()
{
  switch (lfowavenum)
  {
    case 0: // triangle
      lfoval = (lfotimer*lfodelta) >> 8;
      if (lfotimer >= lfofreq/2)
        lfoval = lfodepth - lfoval;
      break;
    case 1: // sawtooth up
      lfoval = (lfotimer*lfodelta) >> 8;
      break;
    case 2: // sawtooth down
      lfoval = lfodepth - ((lfotimer*lfodelta) >> 8);
      break;
    case 3: // square
      lfoval = (lfotimer >= lfofreq/2) ? lfodepth : 0;
      break;
    case 4: // half square
      lfoval = (lfotimer < lfofreq/4) ? lfodepth : 0;
      break;
    case 5: // half sawtooth up
      if (lfotimer < lfofreq/2)
        lfoval = (lfotimer*lfodelta) >> 7;
      else
        lfoval = 0;
      break;
    case 6: // half sawtooth down
      if (lfotimer < lfofreq/2)
        lfoval = lfodepth - ((lfotimer*lfodelta) >> 7);
      else
        lfoval = 0;
      break;
    case 7: // random
      if (lfotimer == 0)
        lfoval = lfsr_rand() % lfodepth;
      break;
  }

  if (lfotimer < lfofreq/2)
    set_bit(PORT(LEDS), LFO_LED_PIN);
  else
    clear_bit(PORT(LEDS), LFO_LED_PIN);

  lfotimer++;
  if (lfotimer >= lfofreq)
    lfotimer = 0;
}

// update envelope state
void update_env()
{
  // increment envelope timer and wrap around
  envtimer++;
  if (envtimer >= envfreq)
    envtimer = 0;

  // determine output value
  envval = (envtimer <= envwidth);
  if (envval)
    set_bit(PORT(LEDS),ENV_LED_PIN);
  else
    clear_bit(PORT(LEDS),ENV_LED_PIN);
}

// update parameters based on knob values
void update_synth_params()
{
  // disregard pitch if midi enabled
  if (!usemidi)
    pitch = knobs[PITCH_KNOB].val;
  
  uint8_t newwavenum = knobs[AUDIO_WF_KNOB].val >> 7;
  if (newwavenum != wavenum)
  {
    wavenum = newwavenum;
    waveform = waveforms[wavenum];
    ledpulse = 1;
  }

  uint8_t newlfowavenum = knobs[LFO_WF_KNOB].val >> 7;
  if (newlfowavenum != lfowavenum)
  {
    lfowavenum = newlfowavenum;
    ledpulse = 1;
  }

  // pulse the power LED to indicate waveform changes
  if (ledpulse)
  {
    clear_bit(PORT(LEDS), PWR_LED_PIN);
    ledpulse--;
  }
  else
    set_bit(PORT(LEDS), PWR_LED_PIN);    
  
  lfofreq = knobs[LFO_FREQ_KNOB].val >> 2;
  lfodepth = knobs[LFO_DEPTH_KNOB].val >> 2;
  lfodelta = (lfofreq) ? (lfodepth*256U) / lfofreq : 0;
  
  envfreq = knobs[ENV_FREQ_KNOB].val >> 2;
  envwidth = knobs[ENV_WIDTH_KNOB].val >> 2;
}

int main(void)
{
  // enable outputs
  DDR(AUDIO) |= _BV(AUDIO_PIN);
  DDR(LEDS)  |= _BV(ENV_LED_PIN) | _BV(LFO_LED_PIN) | _BV(PWR_LED_PIN);
  DDR(MUXCTL) |= _BV(MUXCTL_PIN);

  // turn on power LED
  set_bit(PORT(LEDS), PWR_LED_PIN);

  // set up main oscillator
  TCCR1B |= _BV(WGM12);  // enable CTC
  TIMSK1 |= _BV(OCIE1A); // enable CTC interrupt

  // set up ADC
  ADCSRA |= _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0); // 1/128 prescaler
  ADCSRA |= _BV(ADEN); // enable ADC

  // set up midi
  midi_init();

  sei(); // enable interrupts
 
  lfowavenum = 0;
  wavenum = 0;
  waveform = waveforms[wavenum];

  update_pitch();
  TCCR1B |= _BV(CS11); // set prescaler to 8; starts oscillator  

  adctimer = 0;
  for (;;)
  {
    uint8_t adcchan = 0;

    adctimer++;
    adctimer &= (NUM_ADC_SAMPLES-1);

    // select mux channel group 0
    clear_bit(PORT(MUXCTL), MUXCTL_PIN);

    // read ADC channels
    for (adcchan = 0; adcchan < NUM_ADC_CHANNELS; adcchan++)
    {
      ADMUX = _BV(REFS0) | adcchan;
      ADCSRA |= _BV(ADSC);
      loop_until_bit_is_clear(ADCSRA, ADSC);

      read_adc(knobs+adcchan);
    }
    
    // select mux channel group 1
    set_bit(PORT(MUXCTL), MUXCTL_PIN);
    
    // read muxed ADC channels
    for (adcchan = MUXED_CH_LOW; adcchan <= MUXED_CH_HIGH; adcchan++)
    {
      ADMUX = _BV(REFS0) | adcchan;
      ADCSRA |= _BV(ADSC);
      loop_until_bit_is_clear(ADCSRA, ADSC);

      read_adc(knobs+NUM_ADC_CHANNELS+(adcchan-MUXED_CH_LOW));
    }
 
    // update values after we've read averaged values from all knobs
    if (adctimer == 0)
      update_synth_params();

    update_lfo();
    update_env();
    update_pitch();

    _delay_us(100);
  }

  return 0;
}


// Added to git repository

#include <Arduino.h>
#include <avr/io.h>

#define ad_reset _BV(PB2)
#define ad_clk _BV(PB5)
#define ad_fq_ud _BV(PB1)
#define ad_trig _BV(PB0)
#define mod_disable _BV(PB4)

/*
// stuff for rotary encoder... if I ever get to it :-)
#define mod0 _BV(PD2)
#define mod1 _BV(PD3)
#define mod2 _BV(PD4)
#define mod_advance _BV(PB5)
*/

union freq_word {
  uint32_t l;
  uint8_t s[4];
};

union freq_word fw;        // next 32 bit frequency word to load to AD9850
int inx = 0;               // 8 bit index into frequency word (0 .. 3)
uint32_t sine_table[64];   // Frequency words for one sine wave, 64 slices

//const float ONEHZ = pow(2,32) / 125000820;
const float ONEHZ = pow(2,32) / 125000000;

/*
  FM modulation values for a half wave at 1000 Hz deviation.  A full wave
  table is constructed in the setup function and scaled for the desired
  deviation.  e.g multply by 2.5 for a 2.5kHz deviation
*/
const uint32_t sines1000[] {
    0,3368,6703,9974,13149,16197,19089,21798,
    24296,26560,28569,30303,31744,32880,33700,34194,
    34360,34194,33700,32880,31744,30303,28569,26560,
    24296,21798,19089,16197,13149,9974,6703,3368
    };

/*
  program the AD9850 to generate FM modulation around a 20 mHz center
  frequency.  That will put the second image (fundamental + 125 mHz clock)
  at 145 mHz which is in the amateur radio 2 meter band.  I have a simple
  120 mHz or so high pass filter to cut back the fundamental and first image
  output but need to "make it better" as some clock is leaking through.  The
  good news is, most radios' front ends have bandpass filters that reject
  the unwanted junk.  The down side is the sencond image is low to start
  with (sinc(x) ) and then further attenuated by the high pass filter ... but
  enough energy remains to do the job!
*/

const float fbase_freq = (ONEHZ * 20000000);
const uint32_t base_freq = uint32_t(fbase_freq);

void ad9850_par_load_mode() {
  
/*
  reset the AD9850 and put into into parallel load mode (versus serial load)
  
  Parallel mode requires 5 data ad_clocks per cycle (5 x 8 = 40 bits) versus
  40 data clocks for serial mode.  Serial mode would reduce the maximum
  useable audio tone from 1500Hz to about 190Hz.  The down side of course
  is 7 more gpio pins (and wiring) are required.
*/

  PORTB &= ~(ad_reset);    // clear reset pin
  PORTB &= ~(ad_fq_ud);    // clear fq_ud pin
  PORTB |= ad_reset;       // toggle reset on - off
  PORTB &= ~(ad_reset);  
}

ISR (TIMER1_COMPA_vect) {
     
     PORTB |= ad_trig;  // set scope trigger pin PB0
      
     // Update new frequency word in AD9850, togle fq_ud (writing to PINB toggles pin)
     //   Updates with the new configuration staged in the AD9850 buffer during the
     //   previous interrupt.  Then pre-stage the next frequency word which sits in
     //   the buffer until the next interrupt.
     
     PINB = ad_fq_ud;
       //__asm__ __volatile__ ("nop\n\t");  // make sure we have a nice "fat" pulse
     PINB = ad_fq_ud;

     // load next frequency word from table
     fw.l = sine_table[inx];

     // write phase word - 0x00
     PORTC = 0;
     PORTD &= 0x3f;

     // Toggle clock pin on / off (writing to PINB toggles pin)
     PINB = ad_clk;
       // Stretch the clock
       //__asm__ __volatile__ ("nop\n\t");
     PINB = ad_clk;
    
     // write 32 bit frequency word
     for (int j=3;j > -1;j--) {
          PORTC = (fw.s[j] & 0x3f);
          PORTD &= 0x3f;
          PORTD |= (fw.s[j] & 0xc0);
          PINB = ad_clk;
            //__asm__ __volatile__ ("nop\n\t");
          PINB = ad_clk;
     }

     // Update new frequency word in AD9850, togle fq_ud (writing to PINB toggles pin)
     /*
     PINB = ad_fq_ud;
       //__asm__ __volatile__ ("nop\n\t");
     PINB = ad_fq_ud;
    */

     // Update frequency table pointer to next value, "zero out" if > 63
     inx = (inx + 1) & 0x3F;
            
     PORTB &= ~(ad_trig);   // reset scope trigger pin

}    // ISR

void setup() {
  
  const float deviation = 2.5;   // set the desired deviation in KHz

  // initialize the FM waveform generator table  
  for (int i = 0;i < 32;i++) {
    sine_table[i] = base_freq + sines1000[i] * deviation;
    sine_table[i+32] = base_freq - sines1000[i] * deviation;
    ;
  }

// Set up timer 1 which controls modulation (audio) frequency

  // clear configuratiion bits  bits
  TCCR1A = 0;
  TCCR1B = 0;
  TCCR1C = 0;

  // set Mode 4, CTC on OCR1A  
  TCCR1B |= (1 << WGM12);
  
  // timer enabled, no clock prescalar
  TCCR1B |= (1 << CS10);

  // Enable interrupt on compare match
  TIMSK1 |= (1 << OCIE1A);

// ***** compare register values for interesting frequencies
  //OCR1A = 624;      // 500 hz
  //OCR1A = 312;      // 1000 hz
  //OCR1A = 207;      // 1500 hz
  OCR1A = 188;      // testing, 189 / 1653 Hz is as far as we can go... at 20 mHz

// ***** compare register  values for bessel null frequencies  
  //OCR1A = 750;      // 416 hz   1000 Hz deviation  *****
  //OCR1A = 500;      // 624 hz   1500 Hz deviation  *****  
  //OCR1A = 375;      // 832 hz   2000 Hz deviation  *****
  //OCR1A = 300;      // 1040 hz  2500 Hz deviation  *****
  //OCR1A = 250;      // 1247 Hz  3000 Hz deviation  *****

// disable timer 0 and 2 interrupts.
  TIMSK0 = 0;
  TIMSK2 = 0;

  // Clear port B (control bits)
  PORTB = 0;

  // set required port gpio bits to output in the data direction registers
  DDRB = ad_reset | ad_fq_ud | ad_clk | ad_trig;
  DDRD |= 0xc0;
  DDRC = 0x3f;
  
  // PORTB |= mod_disable;  // activate PB4 pullup for modulation enable

  ad9850_par_load_mode();

  // enable interrupts - let's rock and roll!
  sei();
}

int main() {

  /*
   This declaration overrides the arduino main and replaces the normal loop function
     - we control the horizontal, we control the vertical
     - minimizes timer and interrupt jitter during FM modulation generation
   
   Call the setup function to initialize stuff then all the action occurs in the timer
   compare interrupt fuction.  The timer is set to interrupt 64 times per waveform at
   a rate that generates [close to] the desired audio frequency.  Note that in this
   64 slice version the maximum audio tone is 1653Hz.  The 32 slice version works to
   3000 Hz with slightly less fidelity in the resulting demodulated audio wave / tone.
   This is not noticable to my "tin ear" or casual viewing with an oscilloscope but a
   bassoon player might notice.  I suspect a distortion meter can also tell but this is 
   irrelevant for the generated deviation which is perfect.... if I may say so myself.
  */
  
  setup();
  
  while (true) { }  // loop forever
  return(0);        // never get here.

} // end of main()

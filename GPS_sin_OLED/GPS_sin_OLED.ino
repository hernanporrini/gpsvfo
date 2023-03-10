/*

 Arduino Controlled GPS Corrected Generator
 
 Permission is granted to use, copy, modify, and distribute this software
 and documentation for non-commercial purposes.

 Based on the projects: 
 W3PM (http://www.knology.net/~gmarcus/)
 &
 SQ1GU (http://sq1gu.tobis.com.pl/pl/syntezery-dds/44-generator-si5351a)
 
 */
 
#include <TinyGPS++.h>
#include <string.h>
#include <ctype.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <Wire.h>
#include <si5351.h>
#include <SPI.h>

// The TinyGPS++ object
TinyGPSPlus gps;

// The Si5351 object
Si5351 si5351;

// Definicje czestotliwosci dla zworek Z1 i Z2 oraz Z1&Z2
#define F1 4000000
#define F2 15000000
#define F3 20000000

#define ppsPin                   2
#define przycisk                 A2

// Zworki
#define Z1 4
#define Z2 6
#define LED 12


// configure variables
unsigned long XtalFreq = 100000000;
unsigned long XtalFreq_old = 100000000;
long stab;
long correction = 0;
byte stab_count = 44;
unsigned long mult = 0, Freq = 10000000;
unsigned int tcount = 0;
unsigned int tcount2 = 0;
int validGPSflag = false;
char c;
boolean newdata = false;
boolean GPSstatus = true;
byte new_freq = 1;
unsigned long freq_step = 1000;
byte encoderOLD, menu = 0, band = 1, f_step = 1;
unsigned long pps_correct;
byte pps_valid = 1;
float stab_float = 1000;



//*************************************************************************************
//                                    SETUP
//*************************************************************************************
void setup()
{                   
  pinMode(Z1, INPUT_PULLUP);
  pinMode(Z2, INPUT_PULLUP);
  pinMode(LED, OUTPUT);

  if (!digitalRead(Z1)) Freq = F1;
  if (!digitalRead(Z2)) Freq = F2;
  if ((!digitalRead(Z1))&& (!digitalRead(Z2))) Freq = F3;

  TCCR1B = 0;                                    //Disable Timer5 during setup
  TCCR1A = 0;                                    //Reset
  TCNT1  = 0;                                    //Reset counter to zero
  TIFR1  = 1;                                    //Reset overflow
  TIMSK1 = 1;                                    //Turn on overflow flag
  pinMode(ppsPin, INPUT);                        // Inititalize GPS 1pps input
  digitalWrite(ppsPin, HIGH);


  si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
  si5351.drive_strength(SI5351_CLK1, SI5351_DRIVE_6MA);

  Serial.begin(9600);

  // Set CLK0 to output 2,5MHz
  si5351.set_ms_source(SI5351_CLK0, SI5351_PLLA);
  si5351.set_freq(250000000ULL, SI5351_CLK0);
  si5351.set_ms_source(SI5351_CLK1, SI5351_PLLB);
  si5351.set_freq(Freq * SI5351_FREQ_MULT, SI5351_CLK1);
  si5351.update_status();

  GPSproces(6000);

  if (millis() > 5000 && gps.charsProcessed() < 10) {
    GPSstatus = false;
  }
  if (GPSstatus == true) {
    do {
      GPSproces(1000);
    } while (gps.satellites.value() == 0);

    attachInterrupt(0, PPSinterrupt, RISING);
    TCCR1B = 0;
    tcount = 0;
    mult = 0;
    validGPSflag = 1;
  }
}
//***************************************************************************************
//                                         LOOP
//***************************************************************************************
void loop()
{

  if (tcount2 != tcount) {
    tcount2 = tcount;
    pps_correct = millis();
  }
  if (tcount < 4 ) {
    GPSproces(0);
  } 

  if (new_freq == 1) {
    correct_si5351a();
    new_freq = 0;
    if (abs(stab_float)<1)  {
      digitalWrite(LED, HIGH);
    }
    if (abs(stab_float)>1) {
      digitalWrite(LED, LOW);
    }
  }
  
  if (millis() > pps_correct + 1200) {
    pps_valid = 0;
    pps_correct = millis();
  }
}

//**************************************************************************************
//                       INTERRUPT  1PPS
//**************************************************************************************

void PPSinterrupt()
{
  tcount++;
  stab_count--;
  if (tcount == 4)                               // Start counting the 2.5 MHz signal from Si5351A CLK0
  {
    TCCR1B = 7;                                  //Clock on rising edge of pin 5
    // loop();
  }
  if (tcount == 44)                              //The 40 second gate time elapsed - stop counting
  {
    TCCR1B = 0;                                  //Turn off counter
    if (pps_valid == 1) {
      XtalFreq_old = XtalFreq;
      XtalFreq = mult * 0x10000 + TCNT1;           //Calculate correction factor
      new_freq = 1;
    }
    TCNT1 = 0;                                   //Reset count to zero
    mult = 0;
    tcount = 0;                                  //Reset the seconds counter
    pps_valid = 1;
    Serial.begin(9600);
    stab_count = 44;
    stab_on_lcd();
  }
}
//*******************************************************************************
// Timer 1 overflow intrrupt vector.
//*******************************************************************************
ISR(TIMER1_OVF_vect)
{
  mult++;                                          //Increment multiplier
  TIFR1 = (1 << TOV1);                             //Clear overlow flag
}

//********************************************************************************
//                                STAB on LCD  stabilno??c cz??stotliwo??ci
//********************************************************************************
void stab_on_lcd() {
  long pomocna;
  stab = XtalFreq - 100000000;
  stab = stab * 10 ;
  if (stab > 100 || stab < -100) {
    correction = correction + stab;
  }
  else if (stab > 20 || stab < -20) {
    correction = correction + stab / 2;
  }
  else correction = correction + stab / 4;
  pomocna = (10000 / (Freq / 1000000));
  stab = stab * 100;
  stab = stab / pomocna;
  stab_float = float(stab);
  stab_float = stab_float / 10;
}

//********************************************************************
//             NEW frequency
//********************************************************************
void update_si5351a()
{
  si5351.set_freq(Freq * SI5351_FREQ_MULT, SI5351_CLK1);

}
//********************************************************************
//             NEW frequency correction
//********************************************************************
void correct_si5351a()
{
  si5351.set_correction(correction, SI5351_PLL_INPUT_XO);
  //update_si5351a();

}
//*********************************************************************
//                    Odczyt danych z GPS
//**********************************************************************
static void GPSproces(unsigned long ms)
{
  unsigned long start = millis();
  do
  {
    while (Serial.available())
      gps.encode(Serial.read());
  } while (millis() - start < ms);
}
//*********************************************************************

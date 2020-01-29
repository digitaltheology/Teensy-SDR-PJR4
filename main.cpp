/*
The G0OER Teensy-SDR project.

Builds on the work of VE3MKC/rheslip.github.com, to whom all due respects, kudos, and such like.

For Teensy 4.0 board + Teensy Audio board (SGTL5000)

Needs I/Q feed at IF of 11MHz (as the original VE3MKC design)


*/



#include <Arduino.h>
#include <Metro.h>
#include <Audio.h>
#include <Wire.h>
#include <SD.h>
#include <Encoder.h>
#include <Bounce2.h>
#include "filters.h"
#include "si5351.h"
#include "LiquidCrystal_I2C.h"



//Hardware Value Defines

#define INITIAL_VOLUME 0.5   // 0-1.0 output volume on startup
#define TUNE_STEP       100    // slow tuning rate 100Hz steps
#define SI5351_CORRECTION_FACTOR  -13475    // Si5351 correction factor in parts per 10e8 (hundred million)
#define HARDWARE_IF 11000       // The IF of the Quadrature Detector into the ADC (in Hertz).

//Pin Defines
#define MODE_SWITCH   1
#define BAND_SWITCH   2
#define TUNE_RATE_SWITCH  3
#define ENCODER_A     4
#define ENCODER_B     5

//Basic variables/constants
int32_t correctionFactor = SI5351_CORRECTION_FACTOR;
const int freqMultiplier = 4;                      // Multiply the local oscillator by 4 because it's going into the 74AHC74 div by 4 quadrature
int tuneStep = TUNE_STEP;
unsigned long operatingFrequency = 3650000;  // Initial frequency of operation
const unsigned long intermediateFrequency = HARDWARE_IF;
uint64_t localOscFrequency;             // The actual local oscillator frequency (normally 4 x operatingFrequency)

// Create the Audio components.  These should be created in the
// order data flows, inputs/sources -> processing -> outputs
//		
AudioInputI2S         audioInput;           // Audio Shield: mic or line-in
AudioOutputI2S        audioOutput;          // Audio Shield: headphones & line-out

// FIR filters
AudioFilterFIR        rxHilbert45_I;           // See ./lib/filters for details
AudioFilterFIR        rxHilbert45_Q;
AudioFilterFIR        fir_IF;                   // The IF filter, 2.4kHz wide, either USB or LSB, centred on either 12.5kHz or 9.5kHz, with the IF zero beat at 11kHz

AudioMixer4           weaverSummer;         // Summer (add inputs)
AudioSynthWaveform    osc_IF;               // Local Oscillator
AudioEffectMultiply   mixer_IF;             // Mixer (multiply inputs)


AudioConnection   c1(audioInput, 0, rxHilbert45_I, 0);
AudioConnection   c2(audioInput, 1, rxHilbert45_Q, 0);
AudioConnection   c3(rxHilbert45_I, 0, weaverSummer, 0);
AudioConnection   c4(rxHilbert45_Q, 0, weaverSummer, 1);
AudioConnection   c5(weaverSummer, 0, fir_IF, 0);
AudioConnection   c6(fir_IF, 0, mixer_IF, 0);
AudioConnection   c7(osc_IF, 0, mixer_IF, 1);
AudioConnection   c71(mixer_IF, 0, audioOutput, 0);
AudioConnection   c72(mixer_IF, 0, audioOutput, 1);

AudioControlSGTL5000  audioShield;  // Create an object to control the audio shield.

// Tuning rotary encoder
Encoder tuneRotary(ENCODER_A, ENCODER_B);

// Debouncers for the control pins
Bounce tuneStepButtonBounce = Bounce();

// LCD (20 char x 4 line) on address 0x27
LiquidCrystal_I2C lcd(0x27, 20, 4);

// The si5351 control object
Si5351 si5351;


void setup_RX();
void setfreq();
void dispfreq();
void dispTuneStep();

void setup() {

  //pinMode(TUNE_RATE_SWITCH, INPUT);
  //digitalWrite(TUNE_RATE_SWITCH, HIGH);
  tuneStepButtonBounce.attach(TUNE_RATE_SWITCH, INPUT_PULLUP);
  tuneStepButtonBounce.interval(5);

  // Audio connections require memory to work.
  AudioMemory(64);


  // Enable the audio shield and set the output volume.
  audioShield.enable();
  audioShield.volume(INITIAL_VOLUME);
  audioShield.inputSelect(AUDIO_INPUT_LINEIN);

  Wire.begin();
  si5351.init(SI5351_CRYSTAL_LOAD_10PF, 0, correctionFactor);

  lcd.init();                      // initialize the lcd
  lcd.backlight();

  setfreq();
  dispfreq();
  dispTuneStep();
  setup_RX();

  Serial.begin(9600);   // For debugging purposes

}       // END - setup()

void loop() {

  static int encoder_pos = 0, last_encoder_pos = 0;
  int encoder_change;
  static int encoder_accrue;

  tuneStepButtonBounce.update();    // Check the tune step button.
  
  if (tuneStepButtonBounce.fell()) {
    if (tuneStep < 10000) {
      tuneStep *= 10;
    }
    else {
      tuneStep = 10;
    }
    dispTuneStep();
  }
  
  encoder_pos=tuneRotary.read();  // Read the encoder and process if changed.

  if (encoder_pos != last_encoder_pos) {
    encoder_change = encoder_pos - last_encoder_pos;
    last_encoder_pos = encoder_pos;
    encoder_accrue += encoder_change;
    if ((encoder_accrue > 3) | (encoder_accrue < -3))     // This is to tame the rotary encoder, effectively a divide by 4 on the speed.
    {
    operatingFrequency += (encoder_change * tuneStep);  // tune the master vfo - normal steps
    setfreq();  //Update the Si5351
    dispfreq();  // Display the operating frequency
    encoder_accrue = 0;
    }
  }

}       // END - loop()

void setup_RX()
{
  AudioNoInterrupts();   // Disable Audio while reconfiguring filters

  osc_IF.begin(1.0, (float)intermediateFrequency, WAVEFORM_SINE);
  
  // Initialize the wideband +/-45 degree Hilbert filters
  rxHilbert45_I.begin(RX_hilbertm45, HILBERT_COEFFS);
  rxHilbert45_Q.begin(RX_hilbert45, HILBERT_COEFFS);
  // Initialize the IF band pass filter
  fir_IF.begin(firbpf_lsb, BPF_COEFFS);       // 2.4kHz LSB filter

  AudioInterrupts(); 

}

void setfreq()
{
  localOscFrequency = (operatingFrequency - intermediateFrequency) * freqMultiplier;  // Low side injection to fit IF filters and retain i/q sanity.
  si5351.set_freq((localOscFrequency * 100), SI5351_CLK0);

  Serial.print(operatingFrequency);
  Serial.print(" | ");
  Serial.print(intermediateFrequency);
  Serial.print(" | ");
  Serial.print((operatingFrequency - intermediateFrequency));
  Serial.print(" | ");
  Serial.print((int)localOscFrequency);
  Serial.println();
}

void dispfreq()
{
  lcd.setCursor(0, 0);
  lcd.print(operatingFrequency);
}

void dispTuneStep()
{
  lcd.setCursor(9,0);
  lcd.print("/");
  lcd.print(tuneStep);
  lcd.print(" Hz   ");
}
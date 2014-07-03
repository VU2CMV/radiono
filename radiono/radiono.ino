/*
 * Radiono - The Minima's Main Arduino Sketch
 * Copyright (C) 2013 Ashar Farhan
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of  MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *
 * Modified:
 * An Alternate Tuning Method by: Eldon R. Brown (ERB) - WA0UWH, Apr 25, 2014
 *
 */


/*
 * A Long line, to allow for left right scrole, for program construct alignment --------------------------------------------------------------------
 */

#define __ASSERT_USE_STDERR
#include <assert.h>

/*
 * Wire is only used from the Si570 module but we need to list it here so that
 * the Arduino environment knows we need it.
 */
#include <Wire.h>
#include <LiquidCrystal.h>

#include <avr/io.h>
#include "Si570.h"
#include "debug.h"


//#define RADIONO_VERSION "0.4"
#define RADIONO_VERSION "0.4.erb" // Modifications by: Eldon R. Brown - WA0UWH

/*
 The 16x2 LCD is connected as follows:
    LCD's PIN   Raduino's PIN  PURPOSE      ATMEGA328's PIN
    4           13             Reset LCD    19
    6           12             Enable       18
    11          10             D4           17
    12          11             D5           16
    13           9             D6           15
    14           8             D7           14
*/

#define SI570_I2C_ADDRESS 0x55
//#define IF_FREQ   (0)  // FOR DEBUG ONLY
#define IF_FREQ   (19997000l) //this is for usb, we should probably have the USB and LSB frequencies separately
#define CW_TIMEOUT (600l) // in milliseconds, this is the parameter that determines how long the tx will hold between cw key downs

// When RUN_TESTS is 1, the Radiono will automatically do some software testing when it starts.
// Please note, that those are not hardware tests! - Comment this line to save some space.
//#define RUN_TESTS 1

unsigned long frequency = 14200000;
unsigned long vfoA=14200000L, vfoB=14200000L, ritA, ritB;
unsigned long cwTimeout = 0;

Si570 *vfo;
LiquidCrystal lcd(13, 12, 11, 10, 9, 8);

char b[20], c[20], printBuff[32];

/* tuning pot stuff */
unsigned char refreshDisplay = 0;
unsigned int stepSize = 100;

// Added by ERB
#define MAX_FREQ (30000000LU)

#define DEAD_ZONE (40)

#define CURSOR_MODE (1)
#define DIGIT_MODE (2)
#define MODE_SWITCH_TIME (2000U)

#define NO_CURSOR (0)
#define UNDERLINE (1)
#define UNDERLINE_ WINK (2)
#define BLOCK_BLINK (3)

#define MOMENTARY_PRESS (1)
#define DOUBLE_PRESS (2)
#define LONG_PRESS (3)

#define AUTO_SIDEBAND_MODE (0)
#define UPPER_SIDEBAND_MODE (1)
#define LOWER_SIDEBAND_MODE (2)

int tuningDir = 0;
int tuningPosition = 0;
int freqUnStable = 1;
int tuneMode = CURSOR_MODE;
int tuningPositionDelta = 0;
int cursorDigitPosition=0;
int tuningPositionPrevious = 0;
int cursorCol, cursorRow, cursorMode;
int winkOn;
unsigned long modeSwitchTime;
unsigned long freqPrevious;
char* sideBandText[] = {"Auto SB","USB","LSB"};
int sideBandMode = 0;
// End ERB add


unsigned char locked = 0; //the tuning can be locked: wait until it goes into dead-zone before unlocking it

/* the digital controls */

#define LSB (2)
#define TX_RX (3)
#define CW_KEY (4)

#define BAND_HI (5)
#define PA_BAND_CLK (7)

#define FBUTTON (A3)
#define ANALOG_TUNING (A2)
#define ANALOG_KEYER (A1)

const unsigned long bands[] = {  // Lower and Upper Band Limits
      1800000LU,  2000000LU, // 160m
      3500000LU,  4000000LU, //  80m
      7000000LU,  7300000LU, //  40m
     10100000LU, 10150000LU, //  30m
     14000000LU, 14350000LU, //  20m
     18068000LU, 18168000LU, //  17m
     21000000LU, 21450000LU, //  15m
     24890000LU, 24990000LU, //  12m
     28000000LU, 29700000LU, //  10m
   };

#define BANDS (sizeof(bands)/sizeof(unsigned long)/2)

#define VFO_A 0
#define VFO_B 1

char inTx = 0;
char keyDown = 0;
char isLSB = 0;
char isRIT = 0;
char vfoActive = VFO_A;
/* modes */
unsigned char isManual = 1;
unsigned ritOn = 0;

/* dds ddschip(DDS9850, 5, 6, 7, 125000000LL); */

// ###############################################################################
/* display routines */
void printLine1(char const *c){
  if (strcmp(c, printBuff)){
    lcd.setCursor(0, 0);
    lcd.print(c);
    strcpy(printBuff, c);
  }
}

void printLine2(char const *c){
  lcd.setCursor(0, 1);
  lcd.print(c);
}

// ###############################################################################
void displayFrequency(unsigned long f){
  int mhz, khz, hz;

  mhz = f / 1000000l;
  khz = (f % 1000000l)/1000;
  hz = f % 1000l;
  sprintf(b, "[%02d.%03d.%03d]", mhz, khz, hz);
  printLine1(b);
}

void updateDisplay(){
  char const *vfoStatus[] = { "ERR", "RDY", "BIG", "SML" };
   
  if (refreshDisplay) {
     
      refreshDisplay = false;
      cursorOff();
      sprintf(b, "%08ld", frequency);
      
      // Top Line of LCD
      sprintf(c, "%s:%.2s.%.6s %s",
          vfoActive == VFO_A ? "A" : "B" ,
          b,  b+2,
          ritOn ? "RIT" : "    ");
      printLine1(c);
      
      // Bottom Line of LCD
      sprintf(c, "%s%s %s %s     ",
          isLSB ? "LSB" : "USB",
          sideBandMode > 0 ? "*" : " ",
          inTx ? "TX" : "RX",
          freqUnStable ? "     " : vfoStatus[vfo->status]);
      printLine2(c);
      
      setCursorCRM(11 - (cursorDigitPosition + (cursorDigitPosition>6) ), 0, CURSOR_MODE);
  }
  updateCursor();
}


// -------------------------------------------------------------------------------
void setCursorCRM(int col, int row, int mode) {
  // mode 0 = underline
  // mode 1 = underline wink
  // move 2 = block blink
  // else   = no cursor
  cursorCol = col;
  cursorRow = row;
  cursorMode = mode;
}


// -------------------------------------------------------------------------------
void cursorOff() {
  lcd.noBlink();
  lcd.noCursor();
}


// -------------------------------------------------------------------------------
void updateCursor() {
  
  lcd.setCursor(cursorCol, cursorRow); // Postion Curesor
  
  // Set Cursor Display Mode, Wink On and OFF for DigitMode, Solid for CursorMode
  if (cursorMode == CURSOR_MODE) {
      if (millis() & 0x0200 ) { // Update only once in a while
        lcd.noBlink();
        lcd.cursor();
      }
  }
  else if (cursorMode == DIGIT_MODE) { // Winks Underline Cursor
      if (millis() & 0x0200 ) { // Update only once in a while
        if (winkOn == false) {
          lcd.cursor();
          winkOn = true;
        }
      } else {
        if (winkOn == true) {
          lcd.noCursor();
          winkOn = false;
        }
      }
  }
  else if (cursorMode == BLOCK_BLINK) {
      if (millis() & 0x0200 ) { // Update only once in a while
        lcd.blink();
        lcd.noCursor();
      }
  }
  else {
      if (millis() & 0x0200 ) { // Update only once in a while
        cursorOff();
      }
  }
 
}

// ###############################################################################
void setup() {
  // Initialize the Serial port so that we can use it for debugging
  Serial.begin(115200);
  debug("Radiono starting - Version: %s", RADIONO_VERSION);
  lcd.begin(16, 2);

#ifdef RUN_TESTS
  run_tests();
#endif

  printBuff[0] = 0;
  printLine1("Raduino ");
  lcd.print(RADIONO_VERSION);
  
  // Print just the File Name, Added by ERB
  //printLine2("F: ");
  //char *pch = strrchr(__FILE__,'/')+1;
  //lcd.print(pch);
  //delay(2000);
  printLine2("Multi-FN Btns BB");
  delay(2000);
  

  // The library automatically reads the factory calibration settings of your Si570
  // but it needs to know for what frequency it was calibrated for.
  // Looks like most HAM Si570 are calibrated for 56.320 Mhz.
  // If yours was calibrated for another frequency, you need to change that here
  vfo = new Si570(SI570_I2C_ADDRESS, 56320000);

  if (vfo->status == SI570_ERROR) {
    // The Si570 is unreachable. Show an error for 3 seconds and continue.
    printLine2("Si570 comm error");
    delay(3000);
  }
  printLine2("                ");  // Added: ERB
  

  // This will print some debugging info to the serial console.
  vfo->debugSi570();

  //set the initial frequency
  vfo->setFrequency(26150000L);

  //set up the pins
  pinMode(LSB, OUTPUT);
  pinMode(TX_RX, INPUT);
  pinMode(CW_KEY, OUTPUT);
  pinMode(ANALOG_TUNING, INPUT);
  pinMode(FBUTTON, INPUT);

  //set the side-tone off, put the transceiver to receive mode
  digitalWrite(CW_KEY, 0);
  digitalWrite(TX_RX, 1); //old way to enable the built-in pull-ups
  digitalWrite(ANALOG_TUNING, 1);
  digitalWrite(FBUTTON, 0); // Use an external pull-up of 47K ohm to AREF
  
  tuningPositionPrevious = tuningPosition = analogRead(ANALOG_TUNING);
  refreshDisplay = 1;
}


// ###############################################################################
void setSideband(){
  static int prevSideBandMode;
  
  //if (sideBandMode == prevSideBandMode) return;
  
  prevSideBandMode = sideBandMode;
  
  switch(sideBandMode) {
    case AUTO_SIDEBAND_MODE: // Automatic Side Band Mode
      isLSB = (frequency < 10000000L) ? 1 : 0 ; break;
    case UPPER_SIDEBAND_MODE: // Force USB Mode
      isLSB = 0; break;
    case LOWER_SIDEBAND_MODE: // Force LSB Mode
      isLSB = 1; break;
  } 
  digitalWrite(LSB, isLSB);
}


// ###############################################################################
void setPaBandSignal(){
  // This setup is compatable with the RF386 RF Power Amplifier
  // See: http://www.hfsignals.org/index.php/RF386

  // Bitbang Clock Pulses to Change PA Band Filter
  int band;
  static int prevBand;
  static unsigned long prevFrequency;

  if (frequency == prevFrequency) return;
  prevFrequency = frequency;
  
  band = freq2Band();

  //debug("Band Index = %d", band);
  
  if (band == prevBand) return;
  prevBand = band;
  
  debug("Band Change, Index = %d", band);

  digitalWrite(PA_BAND_CLK, 1);  // Output Reset Pulse for PA Band Filter
  delay(500);
  digitalWrite(PA_BAND_CLK, 0);

  while (band-- > 1) { // Output Clock Pulse to Change PA Band Filter
     delay(50);
     digitalWrite(PA_BAND_CLK, 1);
     delay(50);
     digitalWrite(PA_BAND_CLK, 0);
  }
}

// -------------------------------------------------------------------------------
int freq2Band(){
  
  //debug("Freq = %lu", frequency);
  
  if (frequency <  4000000LU) return 4; //   3.5 MHz
  if (frequency < 10200000LU) return 3; //  7-10 MHz
  if (frequency < 18200000LU) return 2; // 14-18 MHz
  if (frequency < 30000000LU) return 1; // 21-28 MHz
  return 1;
}


// ###############################################################################
void setBandswitch(){
  static unsigned prevFrequency;
  
  //if (frequency == prevFrequency) return;
  //prevFrequency = frequency;
  
  if (frequency >= 15000000L) {
    digitalWrite(BAND_HI, 1);
  }
  else {
    digitalWrite(BAND_HI, 0);
  }
}

// ###############################################################################
void readTuningPot(){
    tuningPosition = analogRead(ANALOG_TUNING);
}



// ###############################################################################
// An Alternate Tuning Strategy or Method
// This method somewhat emulates a normal Radio Tuning Dial
// Tuning Position by Switches on FN Circuit
// Author: Eldon R. Brown - WA0UWH, Apr 25, 2014
void checkTuning() {
  long deltaFreq;
  unsigned long newFreq;
  
  if (!freqUnStable) {
    //we are Stable, so, Set to Non-lock
      locked = 0;
  }
  // Count Down to Freq Stable, i.e. Freq has not changed recently
  if (freqUnStable == 1) refreshDisplay = true;
  freqUnStable = max(--freqUnStable, 0);
  
  // Compute tuningDaltaPosition from tuningPosition
  tuningPositionDelta = tuningPosition - tuningPositionPrevious;
  
  tuningDir = 0;  // Set Default Tuning Directon to neather Right nor Left

  // Check to see if Automatic Digit Change Action is Required, if SO, force the change
  if (tuningPosition < DEAD_ZONE * 2) { // We must be at the Low end of the Tuning POT
    tuningPositionDelta = -DEAD_ZONE;
    delay(100);
    if (tuningPosition > DEAD_ZONE ) delay(100);
  }
  if (tuningPosition > 1023 - DEAD_ZONE * 2) { // We must be at the High end of the Tuning POT
    tuningPositionDelta = DEAD_ZONE; 
    delay(100);
    if (tuningPosition < 1023 - DEAD_ZONE / 8) delay(100);
  }

  // Check to see if Digit Change Action is Required, Otherwise Do Nothing via RETURN 
  if (abs(tuningPositionDelta) < DEAD_ZONE) return;

  tuningDir = tuningPositionDelta < 0 ? -1 : tuningPositionDelta > 0 ? +1 : 0;  
  if (!tuningDir) return;  // If Neather Direction, Abort
  
  if (cursorDigitPosition < 1) return; // Nothing to do here, Abort, Cursor is in Park position

  // Compute deltaFreq based on current Cursor Position Digit
  deltaFreq = tuningDir;
  for (int i = cursorDigitPosition; i > 1; i-- ) deltaFreq *= 10;
  
  newFreq = freqPrevious + deltaFreq;  // Save Least Ditits Mode
  //newFreq = (freqPrevious / abs(deltaFreq)) * abs(deltaFreq) + deltaFreq; // Zero Lesser Digits Mode 
  if (newFreq != frequency) {
      // Update frequency if within range of limits, Avoiding Nagative underRoll of UnSigned Long, and over run MAX_FREQ  
      if (!(newFreq > MAX_FREQ * 2) && !(newFreq > MAX_FREQ)) {
        frequency = newFreq;  
        refreshDisplay = true;
        freqUnStable = 25; // Set to UnStable (non-zero) Because Freq has been changed
      }
      tuningPositionPrevious = tuningPosition; // Set up for the next Iteration
  }
  freqPrevious = frequency;
}


// ###############################################################################
void checkTX(){
  
  if (freqUnStable) return;

  //we don't check for ptt when transmitting cw
  if (cwTimeout > 0)
    return;
    
  if (digitalRead(TX_RX) == 0 && inTx == 0){
    refreshDisplay++;
    inTx = 1;
  }

  if (digitalRead(TX_RX) == 1 && inTx == 1){
    refreshDisplay++;
    inTx = 0;
  }
  
}


// ###############################################################################
/*CW is generated by keying the bias of a side-tone oscillator.
nonzero cwTimeout denotes that we are in cw transmit mode.
*/

void checkCW(){
  
  if (freqUnStable) return;

  if (keyDown == 0 && analogRead(ANALOG_KEYER) < 50){
    //switch to transmit mode if we are not already in it
    if (inTx == 0){
      //put the  TX_RX line to transmit
      pinMode(TX_RX, OUTPUT);
      digitalWrite(TX_RX, 0);
      //give the relays a few ms to settle the T/R relays
      delay(50);
    }
    inTx = 1;
    keyDown = 1;
    digitalWrite(CW_KEY, 1); //start the side-tone
    refreshDisplay++;
  }

  //reset the timer as long as the key is down
  if (keyDown == 1){
     cwTimeout = CW_TIMEOUT + millis();
  }

  //if we have a keyup
  if (keyDown == 1 && analogRead(ANALOG_KEYER) > 150){
    keyDown = 0;
    digitalWrite(CW_KEY, 0);
    cwTimeout = millis() + CW_TIMEOUT;
  }

  //if we have keyuup for a longish time while in cw tx mode
  if (inTx == 1 && cwTimeout < millis()){
    //move the radio back to receive
    digitalWrite(TX_RX, 1);
    //set the TX_RX pin back to input mode
    pinMode(TX_RX, INPUT);
    digitalWrite(TX_RX, 1); //pull-up!
    inTx = 0;
    cwTimeout = 0;
    refreshDisplay++;
  }
}




// ###############################################################################
int btnDown(){
  int val, val2;
  val = analogRead(FBUTTON);
  while (val != val2) { // DeBounce Button Press
    delay(10);
    val2 = val;
    val = analogRead(FBUTTON);
  }
  //debug("Val= %d", val);
  if (val>1000) return 0;
  // 47K Pull-up, and 4.7K switch resistors,
  // Val should be approximately = (btnN×4700)÷(47000+4700)×1023
  //sprintf(c,"Val= %d            ", val); printLine2(c); delay(1000);  // For Debug Only
  if (val > 350) return 7;
  if (val > 300) return 6;
  if (val > 250) return 5;
  if (val > 200) return 4;
  if (val > 150) return 3;
  if (val >  50) return 2;
  return 1;
}

// -------------------------------------------------------------------------------
void deDounceBtnRelease () {
  int i = 2;
  
    while (--i) { // DeBounce Button Release, Check twice
      while (btnDown()){
       delay(20);
      }
    }
}

// ###############################################################################
void checkButton(){
  int btn;
  btn = btnDown();
  if (btn) debug("btn %d", btn);
  
  switch (btn) {
    case 0: return; // Abort
    case 1: decodeFN(btn); break;  
    case 2: decodeMoveCursor(btn); break;    
    case 3: decodeMoveCursor(btn); break;
    case 4: decodeSideBandMode(btn); break;
    case 5: decodeBandUpDown(1); break; // Band Up
    case 6: decodeBandUpDown(-1); break; // Band Down
    case 7: decodeAux(btn); break; // Report Un Used AUX Buttons
    default: return;
  }
}


// ###############################################################################
void decodeBandUpDown(int dir) {
  static unsigned long freqCache[] = { // Set Default Values for Cache
      1900000LU, // 160m
      3600000LU, //  80m
      7125000LU, //  40m
     10120000LU, //  30m
     14150000LU, //  20m
     18110000LU, //  17m
     21200000LU, //  15m
     24930000LU, //  12m
     28300000LU, //  10m
   };
   
   static int sideBandModeCache[BANDS];
   
   int i;
   
   switch (dir) {  // Decode Direction of Band Change
     
     case +1:  // For Band Change, Up
       for (i = 0; i < BANDS; i++) {
         if (frequency <= bands[i*2+1]) {
           if (frequency >= bands[i*2]) {
             // Save Current Ham frequency and sideBandMode
             freqCache[i] = frequency;
             sideBandModeCache[i] = sideBandMode;
             i++;
           }
           // Load From Next Cache
           frequency = freqCache[min(i,BANDS-1)];
           sideBandMode = sideBandModeCache[min(i,BANDS-1)];
           freqPrevious = frequency;
           break;
         }
       }
       break;
     
     case -1:  // For Band Change, Down
       for (i = BANDS-1; i > 0; i--) {
         if (frequency >= bands[i*2]) {
           if (frequency <= bands[i*2+1]) {
             // Save Current Ham frequency and sideBandMode
             freqCache[i] = frequency;
             sideBandModeCache[i] = sideBandMode;
             i--;
           }
           frequency = freqCache[max(i,0)];
           sideBandMode = sideBandModeCache[max(i,0)];
           freqPrevious = frequency;
           break;
         }
       }
       break;
     
   }
   
   freqUnStable = 25; // Set to UnStable (non-zero) Because Freq has been changed
   ritOn = 0;
   setSideband();
   refreshDisplay++;
   updateDisplay();
   deDounceBtnRelease(); // Wait for Release
   refreshDisplay++;
}


// ###############################################################################
void decodeSideBandMode(int btn) {
  sideBandMode++;
  sideBandMode %= 3; // Limit to Three Modes
  setSideband();
  cursorOff();
  sprintf(c,"%s               ", sideBandText[sideBandMode]);
  printLine2(c);
  deDounceBtnRelease(); // Wait for Release
  refreshDisplay++;
  updateDisplay();
  
}
// ###############################################################################
void decodeMoveCursor(int btn) {
  
      tuningPositionPrevious = tuningPosition;
      switch (btn) {
        case 2: cursorDigitPosition++; break;
        case 3: cursorDigitPosition--; break;
      }
      cursorDigitPosition = min(cursorDigitPosition, 7);
      cursorDigitPosition = max(cursorDigitPosition, 0);
      freqPrevious = frequency;
      freqUnStable = false;  // Set Freq is NOT UnStable, as it is Stable
      refreshDisplay++;
      updateDisplay();
      deDounceBtnRelease(); // Wait for Button Release
}

void decodeAux(int btn) {
  //debug("Aux %d", btn);
  cursorOff();
  sprintf(c,"Btn: %d         ", btn);
  printLine2(c);
  deDounceBtnRelease(); // Wait for Button Release
  refreshDisplay++;
  updateDisplay();
}

// ###############################################################################
int getButtonPushMode(int btn) {
  int i, t1, t2;
  
  t1 = t2 = i = 0;

  while (t1 < 30 && btnDown() == btn){
    delay(50);
    t1++;
  }
  while (t2 < 10 && !btnDown()){
    delay(50);
    t2++;
  }

  //if the press is momentary and there is no secondary press
  if (t1 < 10 && t2 > 6){
    return MOMENTARY_PRESS;
  }
  //there has been a double press
  else if (t1 < 10 && t2 <= 6) {
    return DOUBLE_PRESS;
  }
  //there has been a long press
  else if (t1 > 10){
    return LONG_PRESS;
  }
}



// ###############################################################################
void decodeFN(int btn) {
  //if the btn is down while tuning pot is not centered, then lock the tuning
  //and return
  if (freqUnStable) {
    if (locked)
      locked = 0;
    else
      locked = 1;
    return;
  }

  switch (getButtonPushMode(btn)) { 
    case MOMENTARY_PRESS:
      ritOn = !ritOn;
      break;
      
    case DOUBLE_PRESS:
      if (vfoActive == VFO_B) {
        vfoActive = VFO_A;
        vfoB = frequency;
        frequency = vfoA;
      }
      else {
        vfoActive = VFO_B;
        vfoA = frequency;
        frequency = vfoB;
      }
      ritOn = 0;
      refreshDisplay++;
      updateDisplay();
      cursorOff();
      printLine2("VFO swap!   ");
      break;
      
    case LONG_PRESS:
      vfoA = vfoB = frequency;
      ritOn = 0;
      refreshDisplay++;
      updateDisplay();
      cursorOff();
      printLine2("VFOs reset! ");
      break;
    default:
      return;
  }

  refreshDisplay++;
  deDounceBtnRelease(); // Wait for Button Release
}

// ###############################################################################
// ###############################################################################
void loop(){
  static unsigned long previousFrequency;
  
  readTuningPot();
  checkTuning();

  //the order of testing first for cw and then for ptt is important.
  checkCW();
  checkTX();
  checkButton();

  vfo->setFrequency(frequency + IF_FREQ);
  
  setSideband();
  setBandswitch();
  setPaBandSignal();

  updateDisplay();
  
}

#ifdef RUN_TESTS

bool run_tests() {
  /* Those tests check that the Si570 libary is able to understand the
   * register values provided and do the required math with them.
   */
  // Testing for thomas - si570
  {
    uint8_t registers[] = { 0xe1, 0xc2, 0xb5, 0x7c, 0x77, 0x70 };
    vfo = new Si570(registers, 56320000);
    assert(vfo->getFreqXtal() == 114347712);
    delete(vfo);
  }

  // Testing Jerry - si570
  {
    uint8_t registers[] = { 0xe1, 0xc2, 0xb6, 0x36, 0xbf, 0x42 };
    vfo = new Si570(registers, 56320000);
    assert(vfo->getFreqXtal() == 114227856);
    delete(vfo);
  }

  Serial.println("Tests successful!");
  return true;
}

// ###############################################################################
// ###############################################################################
// handle diagnostic informations given by assertion and abort program execution:
void __assert(const char *__func, const char *__file, int __lineno, const char *__sexp) {
  debug("ASSERT FAILED - %s (%s:%i): %s", __func, __file, __lineno, __sexp);
  Serial.flush();
  // Show something on the screen
  lcd.setCursor(0, 0);
  lcd.print("OOPS ");
  lcd.print(__file);
  lcd.setCursor(0, 1);
  lcd.print("Line: ");
  lcd.print(__lineno);
  // abort program execution.
  abort();
}

#endif


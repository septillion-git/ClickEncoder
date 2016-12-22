// ----------------------------------------------------------------------------
// Rotary Encoder Driver with Acceleration
// Supports Click, DoubleClick, Long Click
//
// (c) 2010 karl@pitrich.com
// (c) 2014 karl@pitrich.com
// 
// Timer-based rotary encoder logic by Peter Dannegger
// http://www.mikrocontroller.net/articles/Drehgeber
// ----------------------------------------------------------------------------

#include "ClickEncoder.h"

// ----------------------------------------------------------------------------
// Button configuration (values for 1ms timer service calls)
//
#define ENC_BUTTONINTERVAL    10  // check button every x milliseconds, also debouce time

// ----------------------------------------------------------------------------
// Acceleration configuration (for 1000Hz calls to ::service())
//
#define ENC_ACCEL_TOP      3072   // max. acceleration: *12 (val >> 8)
#define ENC_ACCEL_INC        25
#define ENC_ACCEL_DEC         2

// ----------------------------------------------------------------------------

#if ENC_DECODER != ENC_NORMAL
#  if ENC_HALFSTEP
     // decoding table for hardware with flaky notch (half resolution)
     const int8_t ClickEncoder::table[16] __attribute__((__progmem__)) = { 
       0, 0, -1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, -1, 0, 0 
     };    
#  else
     // decoding table for normal hardware
     const int8_t ClickEncoder::table[16] __attribute__((__progmem__)) = { 
       0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0 
     };    
#  endif
#endif

// ----------------------------------------------------------------------------

ClickEncoder::ClickEncoder(int8_t A, int8_t B, int8_t BTN, uint8_t stepsPerNotch, bool active)
  : doubleClickEnabled(true),buttonHeldEnabled(true), accelerationEnabled(true),
    delta(0), last(0), acceleration(0),
    button(Open), steps(stepsPerNotch),
    pinA(A), pinB(B), pinBTN(BTN), pinsActive(active)
{
  uint8_t configType = (pinsActive == LOW) ? INPUT_PULLUP : INPUT;
  if (pinA >= 0) {pinMode(pinA, configType);}
  if (pinB >= 0) {pinMode(pinB, configType);}
#ifndef WITHOUT_BUTTON
  if (pinBTN >= 0) {pinMode(pinBTN, configType);}
#endif
  
  if (digitalRead(pinA) == pinsActive) {
    last = 3;
  }

  if (digitalRead(pinB) == pinsActive) {
    last ^=1;
  }
}

// ----------------------------------------------------------------------------
#ifndef WITHOUT_BUTTON

ClickEncoder::ClickEncoder(int8_t BTN, bool active)
  : doubleClickEnabled(true),buttonHeldEnabled(true), accelerationEnabled(true),
    delta(0), last(0), acceleration(0),
    button(Open), steps(1),
    pinA(-1), pinB(-1), pinBTN(BTN), pinsActive(active)
{
  uint8_t configType = (pinsActive == LOW) ? INPUT_PULLUP : INPUT;
  if (pinBTN >= 0) {pinMode(pinBTN, configType);}
}
#endif

// ----------------------------------------------------------------------------
// call this every 1 millisecond via timer ISR
//
void ClickEncoder::service(void)
{
  bool moved = false;

  if (pinA >= 0 && pinA >= 0) {
    if (accelerationEnabled) { // decelerate every tick
      if(acceleration >= ENC_ACCEL_DEC){
        acceleration -= ENC_ACCEL_DEC;
      }
      else{
        acceleration = 0;
      }
    }

#if ENC_DECODER == ENC_FLAKY
    last = (last << 2) & 0x0F;

    if (digitalRead(pinA) == pinsActive) {
      last |= 2;
    }

    if (digitalRead(pinB) == pinsActive) {
      last |= 1;
    }

    int8_t tbl = pgm_read_byte(&table[last]); 
    if (tbl) {
      delta += tbl;
      moved = true;
    }
#elif ENC_DECODER == ENC_NORMAL
    int8_t curr = 0;

    if (digitalRead(pinA) == pinsActive) {
      curr = 3;
    }

    if (digitalRead(pinB) == pinsActive) {
      curr ^= 1;
    }
    
    int8_t diff = last - curr;

    if (diff & 1) {            // bit 0 = step
      last = curr;
      delta += (diff & 2) - 1; // bit 1 = direction (+/-)
      moved = true;    
    }
#else
# error "Error: define ENC_DECODER to ENC_NORMAL or ENC_FLAKY"
#endif

    if (accelerationEnabled && moved) {
      // increment accelerator if encoder has been moved
      if (acceleration <= (ENC_ACCEL_TOP - ENC_ACCEL_INC)) {
        acceleration += ENC_ACCEL_INC;
      }
    }
  }
  // handle button
  //
#ifndef WITHOUT_BUTTON
  unsigned long currentMillis = millis();
  if ((pinBTN >= 0)        // check button only, if a pin has been provided
      && ((currentMillis - lastButtonCheck) >= ENC_BUTTONINTERVAL))            // checking button is sufficient every 10-30ms
  {
    lastButtonCheck = currentMillis;
    
    if (digitalRead(pinBTN) == pinsActive) { // key is down
      keyDownTicks++;
      if ((keyDownTicks > (buttonHoldTime / ENC_BUTTONINTERVAL)) && (buttonHeldEnabled)) {
        button = Held;
      }
    }

    if (digitalRead(pinBTN) == !pinsActive) { // key is now up
      if (keyDownTicks > 1) {               //Make sure key was down through 1 complete tick to prevent random transients from registering as click
        if (button == Held) {
          button = Released;
          doubleClickTicks = 0;
        }
        else {
          #define ENC_SINGLECLICKONLY 1
          if (doubleClickTicks > ENC_SINGLECLICKONLY) {   // prevent trigger in single click mode
            if (doubleClickTicks < (buttonDoubleClickTime / ENC_BUTTONINTERVAL)) {
              button = DoubleClicked;
              doubleClickTicks = 0;
            }
          }
          else {
            doubleClickTicks = (doubleClickEnabled) ? (buttonDoubleClickTime / ENC_BUTTONINTERVAL) : ENC_SINGLECLICKONLY;
          }
        }
      }

      keyDownTicks = 0;
    }
  
    if (doubleClickTicks > 0) {
      doubleClickTicks--;
      if (doubleClickTicks == 0) {
        button = Clicked;
      }
    }
  }
#endif // WITHOUT_BUTTON

}

// ----------------------------------------------------------------------------

int16_t ClickEncoder::getValue(void)
{
  int16_t val;
  
  noInterrupts();
  val = delta;

  if (steps == 2) delta = val & 1;
  else if (steps == 4) delta = val & 3;
  else delta = 0; // default to 1 step per notch

  interrupts();
  
  if (steps == 4) val >>= 2;
  if (steps == 2) val >>= 1;

  int16_t r = 0;
  int16_t accel = ((accelerationEnabled) ? (acceleration >> 8) : 0);

  if (val < 0) {
    r -= 1 + accel;
  }
  else if (val > 0) {
    r += 1 + accel;
  }

  return r;
}

// ----------------------------------------------------------------------------

#ifndef WITHOUT_BUTTON
ClickEncoder::Button ClickEncoder::getButton(void)
{
  ClickEncoder::Button ret = button;
  if (button != ClickEncoder::Held && ret != ClickEncoder::Open) {
    button = ClickEncoder::Open; // reset
  }
  return ret;
}
#endif

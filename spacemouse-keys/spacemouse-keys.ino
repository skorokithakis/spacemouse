// This is the arduino source code for the open source space mouse with keys.
// Please read the introduction and history with all contributors here:
// https://github.com/AndunHH/spacemouse

// One good starting point is the work and video by TeachingTech: https://www.printables.com/de/model/864950-open-source-spacemouse-space-mushroom-remix
// Then follow along on github, how we reached this state of the source code.

// The user specific settings, like pin mappings or special configuration variables and sensitivities are stored in config.h.
// Please open config_sample.h, adjust your settings and save it as config.h
#include "config.h"

// Include inbuilt Arduino HID library by NicoHood: https://github.com/NicoHood/HID
#include "HID.h"
//Include math operators for doing better calculation algorithms. Arduino math is a standard library already included.
#include <math.h>
#define sign(x) ((x) < 0 ? -1 : ((x) > 0 ? 1 : 0))  //Define Signum Function

// header file for calibration output and helper routines
#include "calibration.h"

// header file for reading the keys
#include "spaceKeys.h"

// the debug mode can be set during runtime via the serial interface
int debug = STARTDEBUG;

// This portion sets up the communication with the 3DConnexion software. The communication protocol is created here.
// hidReportDescriptor webpage can be found here: https://eleccelerator.com/tutorial-about-usb-hid-report-descriptors/
// Altered physical, logical range to ranges the 3DConnexion software expects by Daniel_1284580.
static const uint8_t _hidReportDescriptor[] PROGMEM = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x08,        // Usage (Multi-Axis)
  0xa1, 0x01,        // Collection (Application)
  0xa1, 0x00,        // Collection (Physical)
  0x85, 0x01,        // Report ID
  0x16, 0xAA, 0xFE,  // Logical Minimum (-350) (0xFEAA in little-endian)
  0x26, 0x5E, 0x01,  // Logical Maximum (350) (0x015E in little-endian)
  0x36, 0x88, 0xFA,  // Physical Minimum (-1400) (0xFA88 in little-endian)
  0x46, 0x70, 0x05,  // Physical Maximum (1400) (0x0570 in little-endian)
  0x09, 0x30,        // Usage (X)
  0x09, 0x31,        // Usage (Y)
  0x09, 0x32,        // Usage (Z)
  0x75, 0x10,        // Report Size (16)
  0x95, 0x03,        // Report Count (3)
  0x81, 0x02,        // Input (variable,absolute)
  0xC0,              // End Collection
  0xa1, 0x00,        // Collection (Physical)
  0x85, 0x02,        // Report ID
  0x16, 0xAA, 0xFE,  // Logical Minimum (-350)
  0x26, 0x5E, 0x01,  // Logical Maximum (350)
  0x36, 0x88, 0xFA,  // Physical Minimum (-1400)
  0x46, 0x70, 0x05,  // Physical Maximum (1400)
  0x09, 0x33,        // Usage (RX)
  0x09, 0x34,        // Usage (RY)
  0x09, 0x35,        // Usage (RZ)
  0x75, 0x10,        // Report Size (16)
  0x95, 0x03,        // Report Count (3)
  0x81, 0x02,        // Input (variable,absolute)
  0xC0,              // End Collection
  0xa1, 0x00,        // Collection (Physical)
  0x85, 0x03,        //  Report ID
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //    Logical Maximum (1)
  0x75, 0x01,        //    Report Size (1)
  0x95, 32,          //    Report Count (24)
  0x05, 0x09,        //    Usage Page (Button)
  0x19, 1,           //    Usage Minimum (Button #1)
  0x29, 32,          //    Usage Maximum (Button #24)
  0x81, 0x02,        //    Input (variable,absolute)
  0xC0,
  0xC0
};

//Please do not change this anymore. Use indipendent sensitivity multiplier.
int totalSensitivity = 350;
// Centerpoint variable to be populated during setup routine.
int centerPoints[8];

//store raw value of the keys, without debouncing
int keyVals[NUMKEYS];
// final value of the keys, after debouncing
uint8_t keyOut[NUMKEYS];

//Modifier Functions
int modifierFunction(int x) {
  //making sure function input never exedes range of -350 to 350
  x = constrain(x, -350, 350);
  double result;
#if (modFunc == 1)
  // using squared function y = x^2*sign(x)
  result = 350 * pow(x / 350.0, 2) * sign(x);  //sign putting out -1 or 1 depending on sign of value. (Is needed because x^2 will always be positive)
#elif (modFunc == 2)
  // using tan function: tan(x)
  result = 350 * tan(x / 350.0);
#elif (modFunc == 3)
  // using squared tan function: tan(x^2*sign(x))
  result = 350 * tan(pow(x / 350.0, 2) * sign(x));  //sign putting out -1 or 1 depending on sign of value. (Is needed because x^2 will always be positive)
#elif (modFunc == 4)
  //using cubed tan function: tan(x^3)
  result = 350 * tan(pow(x / 350.0, 3));
#else
  //MODFUNC == 0 or others...
  //no modification
  result = x;
#endif

  //make sure values between-350 and 350 are allowed
  result = constrain(result, -350, 350);
  //converting doubles to int again
  return (int)round(result);
}

// define an array for reading the analog pins of the joysticks
int pinList[8] = PINLIST;

// Function to read and store analogue voltages for each joystick axis.
void readAllFromJoystick(int *rawReads) {
  for (int i = 0; i < 8; i++) {
    rawReads[i] = analogRead(pinList[i]);
  }
}

void setup() {
// setup the keys e.g. to internal pull-ups
#if NUMKEYS > 0
  setupKeys();
#endif

  // HID protocol is set.
  static HIDSubDescriptor node(_hidReportDescriptor, sizeof(_hidReportDescriptor));
  HID().AppendDescriptor(&node);
  // Begin Seral for debugging
  Serial.begin(250000);
  delay(100);
  Serial.setTimeout(2);  // the serial interface will look for new debug values and it will only wait 2ms
  // Read idle/centre positions for joysticks.
  readAllFromJoystick(centerPoints);
  delay(100);
}

// Function to send translation and rotation data to the 3DConnexion software using the HID protocol outlined earlier. Two sets of data are sent: translation and then rotation.
// For each, a 16bit integer is split into two using bit shifting. The first is mangitude and the second is direction.
void send_command(int16_t rx, int16_t ry, int16_t rz, int16_t x, int16_t y, int16_t z, uint8_t *keys) {
  uint8_t trans[6] = { x & 0xFF, x >> 8, y & 0xFF, y >> 8, z & 0xFF, z >> 8 };
  HID().SendReport(1, trans, 6);
  uint8_t rot[6] = { rx & 0xFF, rx >> 8, ry & 0xFF, ry >> 8, rz & 0xFF, rz >> 8 };
  HID().SendReport(2, rot, 6);

#if (NUMKEYS > 0)
  // LivingTheDream added
  // Send the keys marked as HIDKEYS
  HID().SendReport(3, keys, NUMHIDKEYS);
#endif
}

int rawReads[8], centered[8];
// Declare movement variables as 16 bit integers
// int16_t to match what the HID protocol expects.
int16_t velocity[8];

// set the min and maxvals from the config.h into real variables
int minVals[8] = MINVALS;
int maxVals[8] = MAXVALS;

int tmpInput;  // store the value, the user might input over the serial

void loop() {
  //check if the user entered a debug mode via serial interface
  if (Serial.available()) {
    tmpInput = Serial.parseInt();  // Read from serial interface, if a new debug value has been sent. Serial timeout has been set in setup()
    if (tmpInput != 0) {
      debug = tmpInput;
      if (tmpInput == -1) {
        Serial.println(F("Please enter the debug mode now or while the script is reporting."));
      }
    }
  }

  // Joystick values are read. 0-1023
  readAllFromJoystick(rawReads);

#if NUMKEYS > 0
  // LivingTheDream added reading of key presses
  readAllFromKeys(keyVals);
#endif
  // Report back 0-1023 raw ADC 10-bit values if enabled
  if (debug == 1) {
    debugOutput1(rawReads, keyVals);
  }

  // Subtract centre position from measured position to determine movement.
  for (int i = 0; i < 8; i++) {
    centered[i] = rawReads[i] - centerPoints[i];
  }

  if (debug == 20) {
    calcMinMax(centered);  // debug=20 to calibrate MinMax values
  }

  // Report centered joystick values if enabled. Values should be approx -500 to +500, jitter around 0 at idle
  if (debug == 2) {
    debugOutput2(centered);
  }

  // Filter movement values. Set to zero if movement is below deadzone threshold.
  for (int i = 0; i < 8; i++) {
    if (centered[i] < DEADZONE && centered[i] > -DEADZONE) {
      centered[i] = 0;
    } else {
      if (centered[i] < 0) {  //if the value is smaller 0 ...
        // ... map the value from the [min,0] to [-350,0]
        centered[i] = map(centered[i], minVals[i], 0, -totalSensitivity, 0);
      } else {  // if the value is > 0 ...
        // ... map the values from the [0,max] to [0,+350]
        centered[i] = map(centered[i], 0, maxVals[i], 0, totalSensitivity);
      }
    }
  }

  // Report centered joystick values. Filtered for deadzone. Approx -350 to +350, locked to zero at idle
  if (debug == 3) {
    debugOutput2(centered);
  }

  // transX
  velocity[TRANSX] = (-centered[CY] + centered[AY]) / ((float)TRANSX_SENSITIVITY);
  velocity[TRANSX] = modifierFunction(velocity[TRANSX]);  //recalculate with modifier function

  // transY
  velocity[TRANSY] = (-centered[BY] + centered[DY]) / ((float)TRANSY_SENSITIVITY);
  velocity[TRANSY] = modifierFunction(velocity[TRANSY]);  //recalculate with modifier function

  velocity[TRANSZ] = -centered[AX] - centered[BX] - centered[CX] - centered[DX];
  if (velocity[TRANSZ] < 0) {
    velocity[TRANSZ] = modifierFunction(velocity[TRANSZ] / ((float)NEG_TRANSZ_SENSITIVITY));  //recalculate with modifier function
    if (abs(velocity[TRANSZ]) < GATE_NEG_TRANSZ) {
      velocity[TRANSZ] = 0;
    }
  } else {                                                                                        // pulling the knob upwards is much heavier... smaller factor
    velocity[TRANSZ] = constrain(velocity[TRANSZ] / ((float)POS_TRANSZ_SENSITIVITY), -350, 350);  // no modifier function, just constrain linear!
  }

  // rotX
  velocity[ROTX] = (-centered[CX] + centered[AX]) / ((float)ROTX_SENSITIVITY);
  velocity[ROTX] = modifierFunction(velocity[ROTX]);  //recalculate with modifier function
  if (abs(velocity[ROTX]) < GATE_ROTX) {
    velocity[ROTX] = 0;
  }

  // rotY
  velocity[ROTY] = (-centered[BX] + centered[DX]) / ((float)ROTY_SENSITIVITY);
  velocity[ROTY] = modifierFunction(velocity[ROTY]);  //recalculate with modifier function
  if (abs(velocity[ROTY]) < GATE_ROTY) {
    velocity[ROTY] = 0;
  }

  // rotZ
  velocity[ROTZ] = (centered[AY] + centered[BY] + centered[CY] + centered[DY]) / ((float)ROTZ_SENSITIVITY);
  velocity[ROTZ] = modifierFunction(velocity[ROTZ]);  //recalculate with modifier function
  if (abs(velocity[ROTZ]) < GATE_ROTZ) {
    velocity[ROTZ] = 0;
  }

  // Invert directions if needed
#if INVX > 0
  velocity[TRANSX] = velocity[TRANSX] * -1;
#endif
#if INVY > 0
  velocity[TRANSY] = velocity[TRANSY] * -1;
#endif
#if INVZ > 0
  velocity[TRANSZ] = velocity[TRANSZ] * -1;
#endif
#if INVRX > 0
  velocity[ROTX] = velocity[ROTX] * -1;
#endif
#if INVRY > 0
  velocity[ROTY] = velocity[ROTY] * -1;
#endif
#if INVRZ > 0
  velocity[ROTZ] = velocity[ROTZ] * -1;
#endif

#if NUMKEYS > 0
  evalKeys(keyVals, keyOut);
#endif

  if (debug == 4) {
    debugOutput4(velocity, keyOut);
    // Report translation and rotation values if enabled. Approx -800 to 800 depending on the parameter.
  }
  if (debug == 5) {
    debugOutput5(centered, velocity);
  }
  // Report debug 4 and 5 info side by side for direct reference if enabled. Very useful if you need to alter which inputs are used in the arithmatic above.

  // if the kill-key feature is enabled, rotations or translations are killed=set to zero
#if (NUMKILLKEYS == 2)
  if (keyVals[KILLROT] == LOW) {  // check for the raw keyVal and not keyOut, because keyOut is only 1 for a single iteration. keyVals has inverse Logic due to pull-ups
    // kill rotation
    velocity[ROTX] = 0;
    velocity[ROTY] = 0;
    velocity[ROTZ] = 0;
  }
  if (keyVals[KILLTRANS] == LOW) {
    // kill translation
    velocity[TRANSX] = 0;
    velocity[TRANSY] = 0;
    velocity[TRANSZ] = 0;
  }
#endif

  // report velocity and keys after possible kill-key feature
  if (debug == 6) {
    debugOutput4(velocity, keyOut);
  }

  // Send data to the 3DConnexion software.
  // The correct order for TeachingTech was determined after trial and error
#if SWITCHYZ > 0
  // Original from TT, but 3DConnextion tutorial will not work:
  send_command(velocity[ROTX], velocity[ROTZ], velocity[ROTY], velocity[TRANSX], velocity[TRANSZ], velocity[TRANSY], keyOut);
#else
  // Daniel_1284580 noticed the 3dconnexion tutorial was not working the right way so they got changed
  send_command(velocity[ROTX], velocity[ROTY], velocity[ROTZ], velocity[TRANSX], velocity[TRANSY], velocity[TRANSZ], keyOut);
#endif
}

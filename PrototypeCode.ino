#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AFMotor.h>
#include <math.h>
#include "GravityTDS.h"

// SETPOINT DEFINITIONS
#define TDS_SETPOINT 1500
#define PH_SETPOINT 6.5

// pH CALIBRATION VALUES
#define PH_V4 1.789
#define PH_V7 1.270
#define PH_RANGE 0.2

// PIN DEFINITIONS
#define START_BUTTON A5
#define ESTOP_BUTTON A6
#define PH_PROBE A3
#define TDS_PROBE A2
#define LIQUID_LEVEL_SENSOR 30

// FLOW RATE DEFINITIONS (cm^3 s^-1)
#define PH_RATE 1
#define REVERSE_RATE 8.75
#define FORWARD_RATE 33.16667

//The OLEDs you have in your kit are 0.96" I2C 128x64 Monochrome OLEDs
// 0.96" is the screen size, I2C is the protocol it uses to talk to the Arduino
#define SCREEN_WIDTH 128 // OLED display width, in pixels. 
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


#define OLED_I2C_ADDRESS 0x3C // !!IMPORTANT!!: If OLED does not show any text, switch this value to 0x3D

GravityTDS TDSProbe;

// MOTORS
AF_DCMotor pHPump(4);
AF_DCMotor forwardPump(3);
AF_DCMotor reversePump(2);
AF_DCMotor mixingMotor(1);

void setup() {
  Serial.begin(9600);

  delay(1000); // allow all sensors to stabilize somewhat before starting anything

  // SET CORRECT PIN MODES
  pinMode(START_BUTTON, INPUT_PULLUP);
  pinMode(ESTOP_BUTTON, INPUT_PULLUP);
  TDSProbe.setPin(TDS_PROBE);
  TDSProbe.setAref(5.0);
  TDSProbe.setAdcRange(1024);
  TDSProbe.begin();

  /* This section is used to initilize the OLED Display. If the display.begin() fails, it locks the setup into a loop.
  * You dont need to change anything. You can use this section of the code as is.
  * If you don't see anything on the screen, change the OLED_I2C_ADDRESS defined above to 0x3D 
  */
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) 
  { 
    Serial.println(F("DEBUG: SSD1306 allocation failed, halting"));
    for(;;); // Don't proceed, loop forever if the OLED did not initialize
  }

  // prevent starting if liquid not up to required level

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println(F("Please fill tank to required level."));
  display.display();

  Serial.println("DEBUG: waiting for water level");

  while (digitalRead(LIQUID_LEVEL_SENSOR) == HIGH) { // while no water is detected
    continue; // do nothing
  }

  Serial.println("DEBUG: water level adequate, continuing");

  // Display text on start instructing user to press START to start cycle

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);

  display.println(F("Ready to start. Press the"));
  display.setTextSize(2);
  display.println(F("START (left)"));
  display.setTextSize(1);
  display.println(F("button to start the cycle."));
  display.display();

  Serial.println("DEBUG: waiting for user input");

  while (digitalRead(START_BUTTON) == HIGH) {
    continue; // do nothing until start button is pulled LOW
  }

  Serial.println("DEBUG: starting mixing subassembly");

  // FIRST STEP: START MIXING
  display.clearDisplay();
  display.println(F("Starting mixing..."));
  display.display();

  startMagneticStirring();

  float pH = pHRead();
  float TDS = TDSRead();

  // SECOND STEP: DISPENSE NUTRIENTS
  while (TDS < TDS_SETPOINT) {
    if (digitalRead(ESTOP_BUTTON) == LOW) {
      emergencyStop();
    }
  }

  // THIRD STEP: BALANCE pH

  for (;;) {
    if (digitalRead(ESTOP_BUTTON) == LOW) {
      emergencyStop();
    }

    pH = pHRead();
    TDS = TDSRead();

    displayMeasurements(1, TDS, pH);

    if (pH <= PH_SETPOINT + PH_RANGE && pH >= PH_SETPOINT - PH_RANGE) { // bad
      break;
    } else {
      dispensepHSolution();
    }

    delay(750);
  }

  // FOURTH STEP: CYCLE

  // determine volume to pump
  float volumeToTrough = M_PI * pow(3.5, 2) * 3;

  forwardPump.setSpeed((int)(190 * (REVERSE_RATE / FORWARD_RATE)));
  reversePump.setSpeed(255);
  forwardPump.run(FORWARD);

  delay(1000 * (volumeToTrough / REVERSE_RATE));

  reversePump.run(FORWARD);

  while (TDS > TDS_SETPOINT * 0.90) {
    if (digitalRead(ESTOP_BUTTON) == LOW) {
      emergencyStop();
    }
    
    pH = pHRead();
    TDS = TDSRead();

    displayMeasurements(2, TDS, pH);

    delay(750);
  }

  // FIFTH STEP: END
  forwardPump.run(RELEASE);

  delay(1000 * (volumeToTrough / REVERSE_RATE));

  reversePump.run(RELEASE);

  display.clearDisplay();
  display.setTextSize(1);
  display.println(F("Cycle complete."));
  display.display();

}

// Function principally written by Neil St. Amor
// Modified by Christian Rodriguez-Burns
void startMagneticStirring(void) {
  mixingMotor.setSpeed(0);

  mixingMotor.run(FORWARD);

  Serial.println("DEBUG(startMagneticStirring): ramping up motor");

  // Soft-start ramp: 0 to 255 over ~1.5 seconds
  // Prevents the magnetic coupling from slipping on startup
  for (int speed = 0; speed <= 255; speed += 5) {
    Serial.print("DEBUG(startMagneticStirring): current speed ");
    Serial.println(speed);
    mixingMotor.setSpeed(speed);
    delay(30); 
  }
  
  // Lock at 100% full throttle
  mixingMotor.setSpeed(255);
}

void displayMeasurements(int stage, float tds, float ph) {
  display.clearDisplay();
  display.setTextSize(1);
  switch (stage) {
    case 0:
      display.println(F("Dispensing nutrients..."));
      break;
    case 1:
      display.println(F("Balancing pH..."));
      break;
    case 2:
      display.println(F("Running cycle..."));
      break;
    default:
      display.println(F("Unknown stage!!!"));
      break;
  }
  display.print(F("Current TDS: "));
  display.println(tds);
  display.print(F("Current pH:  "));
  display.println(ph);
  display.display();
}

float pHRead(void) {
  // determine slope
  float slope = (7.0 - 4.0) / (PH_V7 - PH_V4);
  // determine calibration offset
  float calibrationOffset = 7.0 - (slope - PH_V7);

  float probeVoltage = analogRead(PH_PROBE) * (5.0 / 1023.0);

  float pH = (slope * probeVoltage) + calibrationOffset;

  return pH;
}

float TDSRead(void) {
  TDSProbe.update();
  float TDS = TDSProbe.getTdsValue();
  return TDS;
}

void emergencyStop(void) {
  display.clearDisplay();
  display.setTextSize(1);
  display.println(F("STOPPING!"));
  display.display();

  pHPump.run(RELEASE);
  forwardPump.run(RELEASE);
  reversePump.run(RELEASE);

  for (int speed = 255; speed > 0; speed += 0) {
    mixingMotor.setSpeed(speed);
    delay(30); 
  }

  mixingMotor.run(RELEASE);

  display.clearDisplay();
  display.setTextSize(1);
  display.println(F("STOPPED. PLEASE RESET."));
  display.display();


  for(;;); // infinite loop to prevent execution
}

void dispensepHSolution(void) {
  float volume = 1;
  pHPump.setSpeed(255);  // Set the speed of pump. Uses an 8 bit value, 255 = 100% speed.
  pHPump.run(FORWARD);

  delay(1000 * (volume / PH_RATE));

  pHPump.run(RELEASE);
}

void loop() {
  Serial.println("DEBUG: setup() has finished, execution halted");
  for(;;);  // put your main code here, to run repeatedly:

}

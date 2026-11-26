#include "GravityTDS.h"
#include <AFMotor.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <math.h>
#include <Servo.h>

#define INDIVIDUAL_TESTING true

// SETPOINT DEFINITIONS
#define TDS_SETPOINT 1500
#define PH_SETPOINT 6.5

// pH CALIBRATION VALUES
#define PH_V4 1.789
#define PH_V7 1.270
#define PH_RANGE 0.2

// PIN DEFINITIONS
#define START_BUTTON A4
#define ESTOP_BUTTON A5
#define PH_PROBE A3
#define TDS_PROBE A2
#define LIQUID_LEVEL_SENSOR 30

// FLOW RATE DEFINITIONS (cm^3 s^-1)
#define PH_RATE 1
#define REVERSE_RATE 8.75
#define FORWARD_RATE 33.16667

// The OLEDs you have in your kit are 0.96" I2C 128x64 Monochrome OLEDs
//  0.96" is the screen size, I2C is the protocol it uses to talk to the Arduino
#define SCREEN_WIDTH 128 // OLED display width, in pixels.
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1    // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define OLED_I2C_ADDRESS 0x3C

GravityTDS TDSProbe;

// MOTORS
AF_DCMotor pHPump(3);
AF_DCMotor forwardPump(1);
AF_DCMotor reversePump(2);
AF_DCMotor mixingMotor(4);

// Use a positional servo rated for 180 degrees of travel.
const int SERVO_PIN = 10;
const int SERVO_HOME_DEG = 180;
const int SERVO_DISPENSE_DEG = 0;
const unsigned long SERVO_STEP_MS = 20;
const unsigned long SERVO_HOLD_MS = 4000;
Servo servo;

float TDS;
float pH;
float volumeToTrough;

void setup() {
  Serial.begin(9600);

  delay(
      1000); // allow all sensors to stabilize somewhat before starting anything

  // SET CORRECT PIN MODES
  pinMode(START_BUTTON, INPUT_PULLUP);
  pinMode(ESTOP_BUTTON, INPUT_PULLUP);
  digitalWrite(SERVO_PIN, LOW);
  pinMode(SERVO_PIN, OUTPUT);

  // INITIALIZE TDS PROBE
  TDSProbe.setPin(TDS_PROBE);
  TDSProbe.setAref(5.0);
  TDSProbe.setAdcRange(1024);
  TDSProbe.begin();

  /* This section is used to initilize the OLED Display. If the display.begin()
   * fails, it locks the setup into a loop. You dont need to change anything.
   * You can use this section of the code as is. If you don't see anything on
   * the screen, change the OLED_I2C_ADDRESS defined above to 0x3D
   */
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println(F("DEBUG: SSD1306 allocation failed, halting"));
    for (;;)
      ; // Don't proceed, loop forever if the OLED did not initialize
  }

  // prevent starting if liquid not up to required level

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Please fill tank to required level."));
  display.display();

  Serial.println("DEBUG: waiting for water level");

  while (digitalRead(LIQUID_LEVEL_SENSOR) ==
         HIGH) { // while no water is detected
    continue;    // do nothing
  }

  Serial.println("DEBUG: water level adequate, continuing");

  // Display text on start instructing user to press START to start cycle

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  display.println(F("Ready to start. Press"));
  display.setTextSize(2);
  display.println(F("START (L)"));
  display.setTextSize(1);
  display.println(F("button to start the\ncycle."));
  display.display();

  Serial.println("DEBUG: waiting for user input");

  while (digitalRead(START_BUTTON) == HIGH) {
    continue; // do nothing until start button is pulled LOW
  }

  Serial.println("DEBUG: starting mixing subassembly");

  // FIRST STEP: START MIXING
  //mixing();

  // SECOND STEP: NUTRIENT DISPENSING
  //nutrientDispensing();

  // THIRD STEP: BALANCE pH
  //phBalancing();

  // FOURTH STEP: CYCLE
  cycle();

  // FIFTH STEP: END
  /*forwardPump.run(RELEASE);

  delay(1000 * (volumeToTrough / REVERSE_RATE));

  reversePump.run(RELEASE);*/

  display.clearDisplay();
  display.setTextSize(1);
  display.println(F("Cycle complete."));
  display.display();
  delay(10000);
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

void mixing(void) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("Starting mixing..."));
  display.display();

  startMagneticStirring();

  float pH = pHRead();
  float TDS = TDSRead();

  while (true) {
    if (digitalRead(ESTOP_BUTTON) == LOW) {
      emergencyStop();
    }

    pH = pHRead();
    TDS = TDSRead();

    displayMeasurements(2, TDS, pH);

    delay(750);

    Serial.println("LOOP");
  }
}

// Keep checking the emergency stop during both movement and holds.
void waitWithEmergencyStop(unsigned long durationMs) {
  const unsigned long started = millis();
  do {
    if (digitalRead(ESTOP_BUTTON) == LOW) {
      emergencyStop();
    }
  } while (millis() - started < durationMs);
}

void holdNutrientServo(int angle) {
  Serial.print(F("DEBUG(nutrientDispensing): Holding command at "));
  Serial.print(angle);
  Serial.print(F(" degrees for "));
  Serial.print(SERVO_HOLD_MS / 1000);
  Serial.println(F(" seconds."));
  waitWithEmergencyStop(SERVO_HOLD_MS);
}

void moveNutrientServo(int fromAngle, int toAngle) {
  Serial.print(F("DEBUG(nutrientDispensing): Sweeping from "));
  Serial.print(fromAngle);
  Serial.print(F(" to "));
  Serial.print(toAngle);
  Serial.println(F(" degrees."));

  const int step = toAngle > fromAngle ? 1 : -1;
  for (int angle = fromAngle; angle != toAngle;) {
    angle += step;
    servo.write(angle);
    waitWithEmergencyStop(SERVO_STEP_MS);
  }
  holdNutrientServo(toAngle);
}

void nutrientDispensing(void) {
  if (digitalRead(ESTOP_BUTTON) == LOW) {
    emergencyStop();
  }

  // Set the home command before enabling the signal on pin 10.
  servo.write(SERVO_HOME_DEG);
  servo.attach(SERVO_PIN);
  holdNutrientServo(SERVO_HOME_DEG);

  TDS = TDSRead();

  // Keep repeating in the current debug mode.
  while (INDIVIDUAL_TESTING || (TDS < TDS_SETPOINT)) {
    if (digitalRead(ESTOP_BUTTON) == LOW) {
      emergencyStop();
    }

    pH = pHRead();
    TDS = TDSRead();
    displayMeasurements(1, TDS, pH);

    if (digitalRead(ESTOP_BUTTON) == LOW) {
      emergencyStop();
    }
    moveNutrientServo(SERVO_HOME_DEG, SERVO_DISPENSE_DEG);
    moveNutrientServo(SERVO_DISPENSE_DEG, SERVO_HOME_DEG);
  }
}

void cycle(void) {
  float volumeToTrough = M_PI * pow(3.5, 2) * 3;

  int forwardSpeed = (int)(190 * (REVERSE_RATE / FORWARD_RATE));

  Serial.print("DEBUG(cycle): forward speed ");
  Serial.println(forwardSpeed);

  forwardPump.setSpeed(forwardSpeed);
  reversePump.setSpeed(255);
  forwardPump.run(BACKWARD);

  delay(1000 * (volumeToTrough / REVERSE_RATE));

  reversePump.run(FORWARD);

  while (INDIVIDUAL_TESTING || (TDS > TDS_SETPOINT * 0.90)) {
    if (digitalRead(ESTOP_BUTTON) == LOW) {
      emergencyStop();
    }

    pH = pHRead();
    TDS = TDSRead();

    displayMeasurements(2, TDS, pH);

    delay(750);
  }
}

void phBalancing(void) {
  for (;;) {
    if (digitalRead(ESTOP_BUTTON) == LOW) {
      emergencyStop();
    }

    pH = pHRead();
    TDS = TDSRead();

    displayMeasurements(1, TDS, pH);

    if (!INDIVIDUAL_TESTING && (pH <= PH_SETPOINT + PH_RANGE && pH >= PH_SETPOINT - PH_RANGE)) { // bad
      break;
    } else {
      dispensepHSolution();
    }

    delay(750);
  }
}

void displayMeasurements(int stage, float tds, float ph) {
  display.clearDisplay();
  display.setCursor(0, 0);
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
  if (servo.attached()) {
    servo.detach();
    digitalWrite(SERVO_PIN, LOW);
  }
  Serial.println("DEBUG(emergencyStop): Stopping.");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println(F("STOPPING!"));
  display.display();

  pHPump.run(RELEASE);
  forwardPump.run(RELEASE);
  reversePump.run(RELEASE);

  for (int speed = 255; speed >= 0; speed -= 5) {
    Serial.print("DEBUG(emergencyStop): current speed ");
    Serial.println(speed);
    mixingMotor.setSpeed(speed);
    delay(30);
  }


  mixingMotor.run(RELEASE);

  //holdNutrientServo(SERVO_HOME_DEG);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println(F("STOPPED PLEASE RESET"));
  display.display();

  for (;;)
    ; // infinite loop to prevent execution
}

void dispensepHSolution(void) {
  float volume = 1;
  pHPump.setSpeed(
      255); // Set the speed of pump. Uses an 8 bit value, 255 = 100% speed.
  pHPump.run(FORWARD);

  delay(1000 * (volume / PH_RATE));

  pHPump.run(RELEASE);
}

void loop() {
  Serial.println("DEBUG: setup() has finished, execution halted");
  for (;;)
    ; // put your main code here, to run repeatedly:
}

/* 
 * APSC 101 Example Sketch
 * 
 * S.Skripchenko for APSC 101, 29-Sep-2025
 * Edited by M.Malik 12-Sep-2026
 * 
 * Example Sketch to run the Diaphragm Water dPump
 * Works in the same way as motor from DCMotor Test run. It uses the Adafruit AFMotor library
 *  
 */
#include <AFMotor.h>

AF_DCMotor dpump(3);   // Connect to M3 port. You can use Any of M1-M4 port, but ensure correct port is used here.

void setup() 
{
  // No setup code needed since we are only testing the dpump
}

void loop() {
  dpump.setSpeed(255); // Set the speed to maximum for test. For your prorotype, you can slow it down of needed
  dpump.run(FORWARD);
}

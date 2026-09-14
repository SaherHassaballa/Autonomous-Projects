#define AILERON_PIN   4
#define ELEVATOR_PIN  2

#define RUDDER_PIN    36
#define THROTTLE_PIN  39

void setup() {
  Serial.begin(115200);

  analogReadResolution(12);

  delay(1000);
}

void loop() {

  int aileron  = analogRead(AILERON_PIN);
  int elevator = analogRead(ELEVATOR_PIN);
  int rudder   = analogRead(RUDDER_PIN);
  int throttle = analogRead(THROTTLE_PIN);

  // CSV format:
  // Aileron,Elevator,Rudder,Throttle

  Serial.print(aileron);
  Serial.print(",");
  Serial.print(elevator);
  Serial.print(",");
  Serial.print(rudder);
  Serial.print(",");
  Serial.println(throttle);

  delay(20);   // 50 Hz
}
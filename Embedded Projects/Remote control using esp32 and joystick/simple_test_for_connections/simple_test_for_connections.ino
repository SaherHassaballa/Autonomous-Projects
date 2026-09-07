#define VCC_PIN  A5
#define GND_PIN  A4
#define VRX_PIN  A3
#define VRY_PIN  A2
#define SW_PIN   A1

void setup() {
  Serial.begin(115200);

  pinMode(VCC_PIN, OUTPUT);
  digitalWrite(VCC_PIN, HIGH);

  pinMode(GND_PIN, OUTPUT);
  digitalWrite(GND_PIN, LOW);

  pinMode(VRX_PIN, INPUT);
  pinMode(VRY_PIN, INPUT);

  pinMode(SW_PIN, INPUT_PULLUP);

  Serial.println("=== Joystick Test ===");
}

void loop() {
  int x = analogRead(VRX_PIN);
  int y = analogRead(VRY_PIN);
  int sw = digitalRead(SW_PIN);

  Serial.print("VRx: ");
  Serial.print(x);

  Serial.print(" | VRy: ");
  Serial.print(y);

  Serial.print(" | SW: ");

  if (sw == LOW)
    Serial.println("PRESSED");
  else
    Serial.println("RELEASED");

  delay(100);
}
// Motor driver pins (Leonardo)
const int ENA = 5;
const int IN1 = 6;
const int IN2 = 4;

const int ENB = 9;
const int IN3 = 10;
const int IN4 = 11;

void setup() {
  Serial.begin(9600);

  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  pinMode(ENB, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  Serial.println("=== MOTOR PWM CALIBRATION ===");
  Serial.println("Watch wheels and note MIN PWM when they START MOVING");
  Serial.println(" ");
}

void testForward() {
  Serial.println("\n--- FORWARD TEST ---");

  // Direction FORWARD
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);

  for (int p = 0; p <= 255; p++) {
    analogWrite(ENA, p);
    analogWrite(ENB, p);

    Serial.print("PWM: ");
    Serial.println(p);

    delay(600);
  }

  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
  delay(1500);
}

void testBackward() {
  Serial.println("\n--- BACKWARD TEST ---");

  // Direction BACKWARD
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);

  for (int p = 0; p <= 255; p++) {
    analogWrite(ENA, p);
    analogWrite(ENB, p);

    Serial.print("PWM: ");
    Serial.println(p);

    delay(600);
  }

  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
  delay(1500);
}

void loop() {
  testForward();
  testBackward();
  Serial.println("\nRESTART Arduino to test again...");
  while (1);
}

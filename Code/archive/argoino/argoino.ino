
const int ir1 = 7;
const int ir2 = 8;
const int ledPin = 13;

void setup() {
  pinMode(ir1, INPUT);
  pinMode(ir2, INPUT);
  pinMode(ledPin, OUTPUT);
  Serial.begin(9600);
}

void loop() {
  int ir1val = digitalRead(ir1);
  int ir2val = digitalRead(ir2);
  
  if (ir1val == LOW || ir2val == LOW) {
    // Obstacle detected
    digitalWrite(ledPin, LOW);
    Serial.println("Obstacle detected!");
  } else {
    // No obstacle
    digitalWrite(ledPin, HIGH);
    Serial.println("No obstacle.");
  }

  delay(100);  // Small delay for stability
}

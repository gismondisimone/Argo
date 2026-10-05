const int puls1 = 3;
const int puls2 = 3;
const int motor = 8;
const int linearN = 7;
const int linearR = 6;

bool press1 = false;
bool press2 = false;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(puls1, INPUT_PULLUP);
  pinMode(puls2, INPUT_PULLUP);
  pinMode(motor, OUTPUT);
  pinMode(linearN, OUTPUT);
  pinMode(linearR, OUTPUT);

  digitalWrite(motor, LOW);
  digitalWrite(linearN, LOW);
  digitalWrite(linearR, LOW);

  Serial.println("Setup Completed");
}

void loop() {
  press1 = (digitalRead(puls1) == LOW);
  press2 = (digitalRead(puls2) == LOW);

  if (press1) {
    Serial.println("Piece detected, making space...");
    
    digitalWrite(motor, HIGH);
    delay(1000);
    
    digitalWrite(motor, LOW);
    delay(1000);
  }
  if (press2) {
    Serial.println("Linear actuator on");
    digitalWrite(linearR, LOW);
    digitalWrite(linearN, HIGH);
    delay(1000);

    Serial.println("Linear actuator retracting");
    digitalWrite(linearN, LOW);
    digitalWrite(linearR, HIGH);
    delay(1000);
  }

  delay(20); 
}
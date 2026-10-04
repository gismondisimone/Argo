const int puls = 3;     // Usare GPIO 3 (il GPIO 2 è di strapping/boot)
const int motor = 8;    // Usare GPIO 8 (alcune schede C3 hanno il GPIO 10 collegato alla memoria flash)
const int linearA = 7;
const int linearB = 6;

bool press = false;

void setup() {
  // Inizializza la seriale e aspetta un istante per la connessione USB CDC
  Serial.begin(115200);
  delay(1000);

  pinMode(puls, INPUT_PULLUP); // Pulsante collegato tra GPIO 3 e GND
  pinMode(motor, OUTPUT);
  pinMode(linearA, OUTPUT);
  pinMode(linearB, OUTPUT);

  digitalWrite(motor, LOW);
  digitalWrite(linearA, LOW);
  digitalWrite(linearB, LOW);

  Serial.println("--- Avvio completato con successo ---");
}

void loop() {
  // Con INPUT_PULLUP il pulsante premuto legge LOW (0)
  press = (digitalRead(puls) == LOW);

  if (press) {
    Serial.println("Pulsante premuto! Attivazione motore...");
    
    digitalWrite(motor, HIGH);
    delay(1000);
    
    digitalWrite(motor, LOW);
    delay(1000);
  }

  // Piccola pausa fondamentale per lasciare respirare il sistema ed evitare crash del Watchdog
  delay(20); 
}
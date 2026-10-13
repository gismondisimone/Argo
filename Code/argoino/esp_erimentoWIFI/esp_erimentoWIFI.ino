#include <WiFi.h>

WiFiServer Mento(80);
String TaxiReq = "N/A";
String ScanReq = "N/A";

const char* net = "Lil's Galaxy S22";
const char* psw = "Nobodyson";
const String localName = "mento";
IPAddress ipTaxi, ipScan;

bool resolve(const char* name, IPAddress& out) {
  return WiFi.hostByName(name, out) == 1;
}

const int beltButt = 2;
const int scanConn = 3;
const int motor = 8;
const int linearN = 7;
const int linearR = 6;

bool beltStep = false;
bool scann = false;
const int on = HIGH;
const int off = LOW;

const int maxx = 31;
int full = 0;
bool unloaded = false;

// debounced read of an INPUT_PULLUP pin (true = pressed/LOW)
bool readStable(int pin) {
  bool a = digitalRead(pin) == LOW;
  delay(10);
  bool b = digitalRead(pin) == LOW;
  return a && b;
}

void relayInit(int pin) {
  digitalWrite(pin, off);
  pinMode(pin, OUTPUT);
}

void sendTo(const String& message, const IPAddress& receiver) {
  WiFiClient client;
  client.setTimeout(2000);
  if (client.connect(receiver, 80)) {
    client.print("GET /" + localName + "?data=" + message + " HTTP/1.1\r\n");
    client.print("Host: " + receiver.toString() + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.stop();
    Serial.println("Sent to " + receiver.toString());
  } else {
    Serial.println("Failed to connect");
    while (!client.connect(receiver, 80)) {
      delay(1000);
      Serial.println("Retrying connection to " + receiver.toString());
    }
    client.print("GET /" + localName + "?data=" + message + " HTTP/1.1\r\n");
    client.print("Host: " + receiver.toString() + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.stop();
    Serial.println("Sent to " + receiver.toString() + " after retry");
  }
}

void linearFreaky() {
  Serial.println("Linear actuator on");
  digitalWrite(linearR, off);
  delay(50);
  digitalWrite(linearN, on);
  delay(1000);

  Serial.println("Linear actuator retracting");
  digitalWrite(linearN, off);
  delay(50);
  digitalWrite(linearR, on);
  delay(1000);

  digitalWrite(linearR, off);
}

void setup() {
  relayInit(motor);
  relayInit(linearN);
  relayInit(linearR);

  Serial.begin(115200);
  delay(1000);

  pinMode(beltButt, INPUT_PULLUP);
  pinMode(scanConn, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  WiFi.config(IPAddress(10, 118, 94, 72),
              IPAddress(10, 118, 94, 1),
              IPAddress(255, 255, 255, 0));
  WiFi.begin(net, psw);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected, IP: " + WiFi.localIP().toString());

  Mento.begin();
  WiFi.config(IPAddress(10,118,94,72), IPAddress(10,118,94,1),
              IPAddress(255,255,255,0),
              IPAddress(10,118,94,1));          // <- DNS server, important

  while (!resolve("argotaxi.local", ipTaxi)) { delay(1000); Serial.println("resolving argotaxi..."); }
  while (!resolve("argoscan.local", ipScan)) { delay(1000); Serial.println("resolving argoscan..."); }

  Serial.println("Setup Completed");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) { 
    digitalWrite(motor, off);
    WiFi.reconnect();
    delay(1000);
    return;
  }

  bool scann = readStable(scanConn);

  if (scann) {
    unloaded = false;
    
    if (full < maxx) {
      if (readStable(beltButt)) {
        Serial.println("Piece detected, making space...");
        digitalWrite(motor, on);
        delay(1000);
        digitalWrite(motor, off);
        delay(1000);

        full++;
        sendTo(String(full), ipTaxi);

        while (readStable(beltButt)) delay(10);
      }
    } else {
      Serial.println("Conveyor full!");
      sendTo("2000", ipTaxi);
      delay(2000);
    }
  }
  else if (!unloaded) {
    // runs ONCE per unload
    int empty = maxx - full;
    digitalWrite(motor, on);
    delay(empty * 1000);
    digitalWrite(motor, off);

    for (int i = 0; i < full; i++) {
      linearFreaky();
      Serial.println("Piece on plate. Waiting");
      sendTo("1", ipScan);
    }

    full = 0;
    unloaded = true;
  }
}
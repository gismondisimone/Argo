#include <WiFi.h>
#include <ESPmDNS.h>

WiFiServer Mento(80);
String TaxiReq = "N/A";
String ScanReq = "N/A";

const char* net = "Lil's Galaxy S22";
const char* psw = "Nobodyson";
const String localName = "mento";
IPAddress ipTaxi, ipScan;

// Pi hostnames (mDNS, without ".local") and ports of their HTTP servers
const char* taxiHost = "argotaxi";
const char* scanHost = "argoscan";
const int PORT_TAXI = 5000;
const int PORT_SCAN = 5000;
// used only if the mDNS name can't be resolved ("" = no fallback)
const char* taxiFallback = "10.234.61.207";
const char* scanFallback = "";
// max wait for the scanner to finish one piece (scan + classify + bins + paddle)
const unsigned long SCAN_TIMEOUT = 180000;

bool resolve(const char* name, IPAddress& out, const char* fallback) {
  IPAddress ip = MDNS.queryHost(name, 1500);
  if ((uint32_t)ip == 0 && fallback[0]) ip.fromString(fallback);
  if ((uint32_t)ip == 0) return false;
  out = ip;
  return true;
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

void sendTo(const String& message, const IPAddress& receiver, int port) {
  WiFiClient client;
  client.setTimeout(2000);
  if (client.connect(receiver, port)) {
    client.print("GET /" + localName + "?data=" + message + " HTTP/1.1\r\n");
    client.print("Host: " + receiver.toString() + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.stop();
    Serial.println("Sent to " + receiver.toString());
  } else {
    Serial.println("Failed to connect");
    while (!client.connect(receiver, port)) {
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

// Waits for the scanner's "go ahead": a GET sent to this server by the scanner Pi.
// Notifications coming from the taxi Pi are answered but ignored.
void waitScanDone() {
  unsigned long t0 = millis();
  Serial.println("Waiting for the scanner go-ahead...");
  while (millis() - t0 < SCAN_TIMEOUT) {
    WiFiClient c = Mento.available();
    if (c) {
      IPAddress from = c.remoteIP();
      unsigned long t1 = millis();
      while (c.connected() && !c.available() && millis() - t1 < 1000) delay(1);
      while (c.available()) c.read();
      c.print("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK");
      c.stop();
      if (from != ipTaxi) {
        Serial.println("Scanner go-ahead received");
        return;
      }
      Serial.println("Notification from the taxi ignored");
    }
    delay(10);
  }
  Serial.println("Timeout waiting for the scanner, continuing");
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
  WiFi.begin(net, psw);        // DHCP: a phone hotspot changes subnet, so no fixed IP
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected, IP: " + WiFi.localIP().toString());

  MDNS.begin(localName.c_str());   // the Pis reach this ESP as mento.local
  Mento.begin();

  while (!resolve(taxiHost, ipTaxi, taxiFallback)) { delay(1000); Serial.println("resolving argotaxi..."); }
  Serial.println("argotaxi at " + ipTaxi.toString());
  while (!resolve(scanHost, ipScan, scanFallback)) { delay(1000); Serial.println("resolving argoscan..."); }
  Serial.println("argoscan at " + ipScan.toString());

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
        sendTo(String(full), ipTaxi, PORT_TAXI);

        while (readStable(beltButt)) delay(10);
      }
    } else {
      Serial.println("Conveyor full!");
      sendTo("2000", ipTaxi, PORT_TAXI);
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
      sendTo("1", ipScan, PORT_SCAN);
      waitScanDone();
    }

    full = 0;
    sendTo("0", ipTaxi, PORT_TAXI);   // the remote's bar goes back to 0
    unloaded = true;
  }
}
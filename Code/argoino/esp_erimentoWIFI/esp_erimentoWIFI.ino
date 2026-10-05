#include <WiFi.h>
#include <WiFiServer.h>

WiFiServer Mento(80);
String TaxiReq = "N/A";
String ScanReq = "N/A";

const char* net = "Lil's Galaxy S22";
const char* psw = "Nobodyson";
const String localName = "mento";
const String ipTaxi = "argotaxi";
const String ipScan = "argoscan";

const int beltButt = 3;
const int scanConn = 3;
const int motor = 8;
const int linearN = 7;
const int linearR = 6;

bool beltStep = false;
bool scann = false;
int full = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(beltButt, INPUT_PULLUP);
  pinMode(scanConn, INPUT_PULLUP);
  pinMode(motor, OUTPUT);
  pinMode(linearN, OUTPUT);
  pinMode(linearR, OUTPUT);

  digitalWrite(motor, LOW);
  digitalWrite(linearN, LOW);
  digitalWrite(linearR, LOW);

  //server setup
  IPAddress staticIP(10, 118, 94, 72);
  IPAddress gateway(10, 118, 94, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(staticIP, gateway, subnet);

  WiFi.begin(net, psw);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected.");
  Serial.print("ssid: ");
  Serial.println(WiFi.SSID());
  IPAddress ip = WiFi.localIP();
  Serial.print("IP mento: ");
  Serial.println(ip);

  Mento.begin();

  Serial.println("Setup Completed");
}

void sendTo(const String& message, const String& receiver) {
  WiFiClient client;
  if (client.connect(receiver.c_str(), 80)) {
    client.print("GET /" + localName + "?data=" + message + " HTTP/1.1\r\n");
    client.print("Host: " + receiver + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.stop();
    Serial.println("Sent to " + receiver);
  } else {
    Serial.println("Failed to connect");
    while (!client.connect(receiver.c_str(), 80)) {
      delay(1000);
      Serial.println("Retrying connection to " + receiver);
    }
    client.print("GET /" + localName + "?data=" + message + " HTTP/1.1\r\n");
    client.print("Host: " + receiver + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.stop();
    Serial.println("Sent to " + receiver + " after retry");
  }
}

String linearFreaky() {
  Serial.println("Linear actuator on");
  digitalWrite(linearR, LOW);
  digitalWrite(linearN, HIGH);
  delay(1000);

  Serial.println("Linear actuator retracting");
  digitalWrite(linearN, LOW);
  digitalWrite(linearR, HIGH);
  delay(1000);
}

void loop() {
  scann = (digitalRead(scanConn) == LOW);
  if (WiFi.status() == WL_CONNECTED) {
    if (scann) { 
      if (full < 31) {
        beltStep = (digitalRead(beltButt) == LOW);

        if (beltStep) {
          Serial.println("Piece detected, making space...");
          
          digitalWrite(motor, HIGH);
          delay(1000);
          
          digitalWrite(motor, LOW);
          delay(1000);

          full == full + 1;
          sendTo(String(full), ipTaxi);
        }
        delay(20);
      } else {
        Serial.println("Conveyor full! ");
        sendTo("2000", ipTaxi);
      }
    } else {
      digitalWrite(motor, LOW);
      int empty = 31-full;
      digitalWrite(motor, HIGH);
      delay(empty*1000); //dacalibrare
      digitalWrite(motor, LOW);
      for (int i = 0; i <= full; i++) {
        linearFreaky();
        Serial.println("Piece on plate. Waiting");
        sendTo("1", ipScan);
      }
    }
  }
}
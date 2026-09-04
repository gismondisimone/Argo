#include <WiFi.h>
#include <WiFiServer.h>

WiFiServer mentoServer(80);
String argoinoReq = "N/A";

const int infrar = 2;
const int motor = 10;
const int linearA = 7; //da cambiare
const int linearB = 6; //da cambiare
const int scanModule = 3; //da cambiare

const char* net = "Lil's Galaxy S22"; //da cambiare
const char* psw = "Nobodyson"; //da cambiare

String rpiTaxi = "10.118.94.140"; //da cambiare
String rpiScan = "10.118.94.140"; //da cambiare
const String localName = "mento";

bool waitin = true;
int scann = false;
int full = 0;

void setup() {
  pinMode(infrar, INPUT);
  pinMode(scanModule, INPUT);
  pinMode(motor, OUTPUT);
  pinMode(linearA, OUTPUT);
  pinMode(linearB, OUTPUT);
  digitalWrite(motor, LOW);
  digitalWrite(linearA, LOW);
  digitalWrite(linearB, LOW);

  Serial.begin(115200);

  // Set static IP
  IPAddress staticIP(10, 118, 94, 72);
  IPAddress gateway(10, 118, 94, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(staticIP, gateway, subnet);

  WiFi.begin(net, psw);
  Serial.print("Connessione in corso");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnesso alla rete!");
  Serial.print("ssid: ");
  Serial.println(WiFi.SSID());
  IPAddress ip = WiFi.localIP();
  Serial.print("IP mento: ");
  Serial.println(ip);

  mentoServer.begin();
}

void loop() {
  scann = digitalRead(scanModule);
  if (WiFi.status() == WL_CONNECTED) {  
    if (scann == 0) { 
      if (waitin) {
        if (full < 30) {
          int dist = analogRead(infrar);
          Serial.println(dist);
          if (dist > 300) {
            Serial.println("Object detected");
            full = full + 1;
            sendTo("1000", rpiTaxi);
            digitalWrite(motor, HIGH);
            delay(3000);
            digitalWrite(motor, LOW);
            Serial.println("Made space.");
            sendTo(String(full), rpiTaxi);
          } else {
            digitalWrite(motor, LOW);
            Serial.println("libero");
            delay(100);
          }
        }else{
          Serial.print("Conveyor full! Please head back to the base to scan.");
          sendTo("2000", rpiTaxi);
        }
        delay(100);
      }
    } else {
      digitalWrite(motor, LOW);
      waitin = false;
      int empty = 30-full;
      digitalWrite(motor, HIGH);
      delay(empty*1000);
      digitalWrite(motor, LOW);
      for (int i = 0; i <= full; i++) {
        digitalWrite(linearA, HIGH);
        digitalWrite(linearB, LOW);
        delay(1000);
        digitalWrite(linearA, LOW);
        digitalWrite(linearB, HIGH);
        delay(1000);
        Serial.println("Piece on plate. Waiting");
        sendTo("1", rpiScan);

      }
    }

    delay(100);
    WiFiClient mentoclnt = mentoServer.available();
    //Serial.println("listening mode");

    if (mentoclnt) {
      Serial.println("connecting");
      String request = "";
      while (mentoclnt.connected()) {
        Serial.println("received");
        Serial.println("scanner free, restarting conveyor...");
        waitin = false; 
        digitalWrite(motor, HIGH);
        mentoclnt.println("HTTP/1.1 200 OK");
        mentoclnt.println("Connection: close");
        mentoclnt.println();
        break;
//        if (mentoclnt.available()) {
//          char c = mentoclnt.read();
//          request += c;
//
//          if (c == '\n') {
//            Serial.println(request);
//            argoinoReq = extractData(request);
//            if (argoinoReq == "oc") {
//              Serial.println("scanner free, restarting conveyor...");
//              waitin = false; 
//              digitalWrite(motor, HIGH);
//            }
//            mentoclnt.println("HTTP/1.1 200 OK");
//            mentoclnt.println("Connection: close");
//            mentoclnt.println();
//            break;
//          }
//        }
        delay(100);
      }
      mentoclnt.stop();
    }
  }
  delay(100);
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

String extractData(String req) {
  int start = req.indexOf("data=") + 5;
  int end = req.indexOf(" ", start);
  return req.substring(start, end);
}

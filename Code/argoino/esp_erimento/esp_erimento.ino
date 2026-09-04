#include <WiFi.h>
#include <WiFiServer.h>

WiFiServer mentoServer(80);
String argoinoReq = "N/A";

const int infrar = 2;
const int motor = 10;

const char* net = "Lil's Galaxy S22"; //da cambiare
const char* psw = "Nobodyson"; //da cambiare

const char* argoinoIP = "10.118.94.140"; //da cambiare
const String localName = "mento";

bool waitin = true;
int full = 0;

void setup() {
  pinMode(infrar, INPUT);
  pinMode(motor, OUTPUT);
  digitalWrite(motor, LOW);

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
  if (WiFi.status() == WL_CONNECTED) {
    if(waitin){
      if(full<30){
        int dist = analogRead(infrar);
        Serial.println(dist);
        if (dist > 300) {
          Serial.println("Object detected");
          delay(10);
          digitalWrite(motor, LOW);
          full = full + 1;
          sendToIno("1");
          waitin = true;
          digitalWrite(motor, HIGH);
          delay(3000);
          digitalWrite(motor, LOW);
          Serial.println("Made space..");
        }else{
          Serial.print("Conveyor full! Please head back to the base to scan.");
          sendToIno("0");
        }
      } else {
        digitalWrite(motor, LOW);
        Serial.println("[mento] = libero");
      }
      delay(100);
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

String sendToIno(char* message) {
  WiFiClient client;
  if (client.connect(argoinoIP, 80)) {
    client.print("GET /" + localName + "?data=" + message + "HTTP/1.1\r\n");
    client.print("Host: " + String(argoinoIP) + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.stop();
    Serial.println("Sent to Arduino");
  } else {
    Serial.println("Failed to connect");
    while (!client.connect(argoinoIP, 80)) {
      delay(1000);
      Serial.println("Retrying connection to Arduino");
    }
    client.print("GET /" + localName + "?data=" + message + "HTTP/1.1\r\n");
    client.print("Host: " + String(argoinoIP) + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.stop();
    Serial.println("Sent to Arduino after retry");
  }
}

String extractData(String req) {
  int start = req.indexOf("data=") + 5;
  int end = req.indexOf(" ", start);
  return req.substring(start, end);
}
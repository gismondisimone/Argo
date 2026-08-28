/*
  Configure a UWB module as a tag using an ESP32 and Arduino IDE.

  WIRING (ESP32 <-> UWB module)
    GPIO 17 (ESP32 UART TX) -> UWB RX
    GPIO 16 (ESP32 UART RX) <- UWB TX
    GND                     -> UWB GND
    3V3                     -> UWB VCC

  Use 3.3 V power and UART logic. Do not connect a 5 V UART signal directly
  to the ESP32. Change UWB_TX_PIN/UWB_RX_PIN below if your board needs other
  available pins. Upload and run this sketch once for the UWB tag.

  CHANNEL and DATA_RATE must match the base. Every tag needs a unique ID;
  use values 1 through 7 because the base uses ID 0.
*/

#include <HardwareSerial.h>

constexpr int UWB_RX_PIN = 16;  // ESP32 input: connected to UWB TX
constexpr int UWB_TX_PIN = 17;  // ESP32 output: connected to UWB RX
constexpr uint32_t UWB_BAUDRATE = 115200;

constexpr uint8_t BOARD_ID = 0; 
constexpr uint8_t BOARD_MODE_TAG = 0;
constexpr uint8_t CHANNEL = 1;    // Must match the base.
constexpr uint8_t DATA_RATE = 1;  // Must match the base.

HardwareSerial uwbSerial(2);  // ESP32 UART2

void printResponse(uint32_t waitMs) {
  delay(waitMs);
  while (uwbSerial.available()) {
    Serial.write(uwbSerial.read());
  }
}

void sendCommand(const char *command, uint32_t waitMs = 1000) {
  Serial.print("> ");
  Serial.println(command);
  uwbSerial.print(command);
  uwbSerial.print("\r\n");
  printResponse(waitMs);
}

void setup() {
  Serial.begin(115200);
  uwbSerial.begin(UWB_BAUDRATE, SERIAL_8N1, UWB_RX_PIN, UWB_TX_PIN);
  delay(500);

  char command[32];
  snprintf(command, sizeof(command), "AT+SETCFG=%u,%u,%u,%u", BOARD_ID,
           BOARD_MODE_TAG, CHANNEL, DATA_RATE);
  sendCommand(command);
  sendCommand("AT+SAVE", 3000);
  sendCommand("AT+GETCFG", 300);

  Serial.println("UWB tag configuration complete.");
}

void loop() {
  // AT+SAVE persists the configuration; no repeated work is needed.
}

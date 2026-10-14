#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <SPI.h>

#define WIFI_SSID      "Lil's Galaxy S22"
#define WIFI_PASS      "Nobodyson"
#define PI_HOST        "argotaxi"          /* Pi hostname, resolved with mDNS */
#define PI_IP_FALLBACK "10.234.61.207"     /* used if the name can't be resolved */
#define PI_PORT        5000

#define TFT_SCK   4    /* SCL */
#define TFT_MOSI  6    /* SDA */
#define TFT_RST   3
#define TFT_DC    5
#define TFT_CS    7

/*don't hold GPIO2 while flashig*/
static const uint8_t botton[4]   = {0, 1, 10, 2};
static const char*   purpose[4]  = {"left", "ahead", "back", "right"};
static const char*   btt_label[4] = {"LEFT", "AHEAD", "BACK", "RIGHT"};

#define TFT_SPI_HZ   10000000
#define TFT_W        240
#define TFT_H        280
#define TFT_Y_OFF    20   /* 240x280 panel sits at rows 20..299 of the 240x320 RAM. Try 0 if shifted. */

#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_RED    0xF800
#define C_GREEN  0x07E0
#define C_YELLOW 0xFFE0
#define C_GRAY   0x2104

/*Display: ST7789 over hardware SPI*/
static SPISettings tftSpi(TFT_SPI_HZ, MSBFIRST, SPI_MODE0);

static void tft_cmd(uint8_t c){
    SPI.beginTransaction(tftSpi);
    digitalWrite(TFT_CS, LOW); digitalWrite(TFT_DC, LOW);
    SPI.transfer(c);
    digitalWrite(TFT_CS, HIGH);
    SPI.endTransaction();
}
static void tft_dat(uint8_t d){
    SPI.beginTransaction(tftSpi);
    digitalWrite(TFT_CS, LOW); digitalWrite(TFT_DC, HIGH);
    SPI.transfer(d);
    digitalWrite(TFT_CS, HIGH);
    SPI.endTransaction();
}
/* Start a pixel stream: caller sends data with tft_write() and finishes with tft_end(). */
static void tft_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1){
    y0 += TFT_Y_OFF; y1 += TFT_Y_OFF;
    tft_cmd(0x2A); tft_dat(x0 >> 8); tft_dat(x0); tft_dat(x1 >> 8); tft_dat(x1);
    tft_cmd(0x2B); tft_dat(y0 >> 8); tft_dat(y0); tft_dat(y1 >> 8); tft_dat(y1);
    tft_cmd(0x2C);
    SPI.beginTransaction(tftSpi);
    digitalWrite(TFT_CS, LOW); digitalWrite(TFT_DC, HIGH);
}
static void tft_write(const uint8_t *b, uint32_t n){ SPI.writeBytes(b, n); }
static void tft_end(void){ digitalWrite(TFT_CS, HIGH); SPI.endTransaction(); }

static void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t col){
    static uint8_t lb[480];
    uint32_t n = (uint32_t)w * h, i, chunk;
    if(!n) return;
    chunk = n < 240 ? n : 240;
    for(i = 0; i < chunk; i++){ lb[2*i] = col >> 8; lb[2*i+1] = col; }
    tft_window(x, y, x + w - 1, y + h - 1);
    while(n){ uint32_t c = n < 240 ? n : 240; tft_write(lb, c * 2); n -= c; }
    tft_end();
}

/* Typical ST7789V2 1.69" init: {cmd, nargs, args...}, terminated by cmd 0. */
static const uint8_t init_tbl[] = {
    0x36,1,0x00,                               /* MADCTL: normal orientation, RGB */
    0x3A,1,0x05,                               /* 16 bit/pixel */
    0xB2,5,0x0B,0x0B,0x00,0x33,0x35,           /* porch */
    0xB7,1,0x11,                               /* gate control */
    0xBB,1,0x35,                               /* VCOM */
    0xC0,1,0x2C,
    0xC2,1,0x01,
    0xC3,1,0x0D,
    0xC4,1,0x20,
    0xC6,1,0x13,                               /* frame rate */
    0xD0,2,0xA4,0xA1,
    0xD6,1,0xA1,
    0xE0,14,0xF0,0x06,0x0B,0x0A,0x09,0x26,0x29,0x33,0x41,0x18,0x16,0x15,0x29,0x2D,
    0xE1,14,0xF0,0x04,0x08,0x08,0x07,0x03,0x28,0x32,0x40,0x3B,0x19,0x18,0x2A,0x2E,
    0xE4,3,0x25,0x00,0x00,
    0x21,0,                                    /* INVON (IPS panels need it) */
    0x00
};

static void tft_init(void){
    const uint8_t *p = init_tbl; uint8_t c, n;

    pinMode(TFT_CS, OUTPUT); pinMode(TFT_DC, OUTPUT); pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    SPI.begin(TFT_SCK, -1, TFT_MOSI, -1);

    digitalWrite(TFT_RST, HIGH); delay(10);
    digitalWrite(TFT_RST, LOW);  delay(20);
    digitalWrite(TFT_RST, HIGH); delay(120);

    while((c = *p++) != 0){ n = *p++; tft_cmd(c); while(n--) tft_dat(*p++); }

    tft_cmd(0x11); delay(120);   /* sleep out */
    tft_cmd(0x29); delay(20);    /* display on */
}

/* 5x7 font, column-major, bit0 = top row. */
typedef struct { char c; uint8_t col[5]; } glyph_t;
static const glyph_t font[] = {
    {' ',{0x00,0x00,0x00,0x00,0x00}},
    {'%',{0x23,0x13,0x08,0x64,0x62}},
    {'-',{0x08,0x08,0x08,0x08,0x08}},
    {':',{0x00,0x36,0x36,0x00,0x00}},
    {'.',{0x00,0x60,0x60,0x00,0x00}},
    {'!',{0x00,0x00,0x5F,0x00,0x00}},
    {'0',{0x3E,0x51,0x49,0x45,0x3E}},
    {'1',{0x00,0x42,0x7F,0x40,0x00}},
    {'2',{0x42,0x61,0x51,0x49,0x46}},
    {'3',{0x21,0x41,0x45,0x4B,0x31}},
    {'4',{0x18,0x14,0x12,0x7F,0x10}},
    {'5',{0x27,0x45,0x45,0x45,0x39}},
    {'6',{0x3C,0x4A,0x49,0x49,0x30}},
    {'7',{0x01,0x71,0x09,0x05,0x03}},
    {'8',{0x36,0x49,0x49,0x49,0x36}},
    {'9',{0x06,0x49,0x49,0x29,0x1E}},
    {'A',{0x7E,0x11,0x11,0x11,0x7E}},
    {'B',{0x7F,0x49,0x49,0x49,0x36}},
    {'C',{0x3E,0x41,0x41,0x41,0x22}},
    {'D',{0x7F,0x41,0x41,0x22,0x1C}},
    {'E',{0x7F,0x49,0x49,0x49,0x41}},
    {'F',{0x7F,0x09,0x09,0x09,0x01}},
    {'G',{0x3E,0x41,0x49,0x49,0x7A}},
    {'H',{0x7F,0x08,0x08,0x08,0x7F}},
    {'I',{0x00,0x41,0x7F,0x41,0x00}},
    {'J',{0x20,0x40,0x41,0x3F,0x01}},
    {'K',{0x7F,0x08,0x14,0x22,0x41}},
    {'L',{0x7F,0x40,0x40,0x40,0x40}},
    {'M',{0x7F,0x02,0x0C,0x02,0x7F}},
    {'N',{0x7F,0x04,0x08,0x10,0x7F}},
    {'O',{0x3E,0x41,0x41,0x41,0x3E}},
    {'P',{0x7F,0x09,0x09,0x09,0x06}},
    {'Q',{0x3E,0x41,0x51,0x21,0x5E}},
    {'R',{0x7F,0x09,0x19,0x29,0x46}},
    {'S',{0x46,0x49,0x49,0x49,0x31}},
    {'T',{0x01,0x01,0x7F,0x01,0x01}},
    {'U',{0x3F,0x40,0x40,0x40,0x3F}},
    {'V',{0x1F,0x20,0x40,0x20,0x1F}},
    {'W',{0x3F,0x40,0x38,0x40,0x3F}},
    {'X',{0x63,0x14,0x08,0x14,0x63}},
    {'Y',{0x07,0x08,0x70,0x08,0x07}},
    {'Z',{0x61,0x51,0x49,0x45,0x43}}
};
static const uint8_t* glyph(char ch){
    for(uint8_t i = 0; i < sizeof(font) / sizeof(font[0]); i++) if(font[i].c == ch) return font[i].col;
    return font[0].col;
}
static void draw_char(uint16_t x, uint16_t y, char ch, uint8_t sc, uint16_t fg, uint16_t bg){
    static uint8_t cbuf[2400];            /* 6*5 x 8*5 pixels * 2 bytes = max scale 5 */
    const uint8_t *g = glyph(ch);
    uint32_t k = 0;
    for(uint8_t row = 0; row < 8; row++)
        for(uint8_t sy = 0; sy < sc; sy++)
            for(uint8_t cx = 0; cx < 6; cx++){
                uint16_t c = (cx < 5 && row < 7 && ((g[cx] >> row) & 1)) ? fg : bg;
                for(uint8_t sx = 0; sx < sc; sx++){ cbuf[k++] = c >> 8; cbuf[k++] = c; }
            }
    tft_window(x, y, x + 6 * sc - 1, y + 8 * sc - 1);
    tft_write(cbuf, k);
    tft_end();
}
/* Draws exactly `width` character cells (pads with spaces) so old text is overwritten. */
static void draw_text(uint16_t x, uint16_t y, const char *s, uint8_t sc, uint16_t fg, uint16_t bg, uint8_t width){
    uint8_t n = 0;
    while(n < width){
        char ch = ' ';
        if(*s){ ch = *s++; if(ch >= 'a' && ch <= 'z') ch -= 32; }
        draw_char(x + n * 6 * sc, y, ch, sc, fg, bg);
        n++;
    }
}

/* ------------------------------------------------------------------ */
/* UI: status line, big percentage, progress bar, last key             */
/* ------------------------------------------------------------------ */
#define BAR_X 10
#define BAR_Y 170
#define BAR_W 220
#define BAR_H 40

static void ui_frame(void){
    fill_rect(0, 0, TFT_W, TFT_H, C_BLACK);
    fill_rect(BAR_X, BAR_Y, BAR_W, 2, C_WHITE);
    fill_rect(BAR_X, BAR_Y + BAR_H - 2, BAR_W, 2, C_WHITE);
    fill_rect(BAR_X, BAR_Y, 2, BAR_H, C_WHITE);
    fill_rect(BAR_X + BAR_W - 2, BAR_Y, 2, BAR_H, C_WHITE);
    fill_rect(BAR_X + 2, BAR_Y + 2, BAR_W - 4, BAR_H - 4, C_GRAY);
}

/* state: 0 idle (white), 1 moving (yellow), 2 alert (red), 3 no link (red), 4 connecting (yellow) */
static uint8_t d_state = 255, d_pct = 255;
static char d_text[24] = "";
static void ui_update(uint8_t state, uint8_t pct, const char *text){
    if(state != d_state || strcmp(text, d_text) != 0){
        uint16_t col = C_WHITE;
        if(state == 1 || state == 4) col = C_YELLOW;
        else if(state == 2 || state == 3) col = C_RED;
        draw_text(10, 20, text, 2, col, C_BLACK, 18);
        d_state = state;
        strncpy(d_text, text, sizeof(d_text) - 1); d_text[sizeof(d_text) - 1] = 0;
    }
    if(pct != d_pct){
        char b[5]; uint16_t inner = BAR_W - 4, filled = (uint32_t)pct * inner / 100;
        b[0] = (pct >= 100) ? '1' : ' ';
        b[1] = (pct >= 10) ? (char)('0' + (pct / 10) % 10) : ' ';
        b[2] = (char)('0' + pct % 10);
        b[3] = '%'; b[4] = 0;
        draw_text(10, 100, b, 5, C_WHITE, C_BLACK, 4);
        fill_rect(BAR_X + 2, BAR_Y + 2, filled, BAR_H - 4, C_GREEN);
        fill_rect(BAR_X + 2 + filled, BAR_Y + 2, inner - filled, BAR_H - 4, C_GRAY);
        d_pct = pct;
    }
}
/* Shown under the bar once the Pi has acknowledged the command. */
static void ui_key(uint8_t idx){
    draw_text(10, BAR_Y + BAR_H + 15, btt_label[idx], 3, C_WHITE, C_BLACK, 8);
}

/* ------------------------------------------------------------------ */
/* Buttons: debounced, returns the held button (0..3) or -1            */
/* ------------------------------------------------------------------ */
static int8_t read_dir(void){
    static int8_t last = -1, stable = -1;
    static uint8_t cnt = 0;
    int8_t now = -1;
    for(uint8_t i = 0; i < 4; i++) if(digitalRead(botton[i]) == LOW){ now = i; break; }
    if(now == last){ if(cnt < 2) cnt++; if(cnt >= 2) stable = now; }
    else { last = now; cnt = 0; }
    return stable;
}

/* ------------------------------------------------------------------ */
/* Network                                                              */
/* ------------------------------------------------------------------ */
static IPAddress piIP;                 /* 0.0.0.0 until resolved */
static bool mdnsOn = false;
static uint8_t miss = 0;
static uint8_t g_state = 0, g_pct = 0;
static char g_text[24] = "IDLE";

static void resolve_pi(void){
    IPAddress ip;
    if(!mdnsOn) mdnsOn = MDNS.begin("tag-esp32");
    if(mdnsOn) ip = MDNS.queryHost(PI_HOST, 1500);
    if((uint32_t)ip == 0) ip.fromString(PI_IP_FALLBACK);
    piIP = ip;
    Serial.print("Pi at "); Serial.println(piIP);
}

/* Pi answers "status,percent,text" to both /display and /drive. */
static void apply_display(String b){
    int c1 = b.indexOf(','), c2 = b.indexOf(',', c1 + 1);
    if(c1 < 0 || c2 < 0) return;
    g_state = (uint8_t)b.substring(0, c1).toInt();
    g_pct = (uint8_t)constrain(b.substring(c1 + 1, c2).toInt(), 0, 100);
    String t = b.substring(c2 + 1);
    t.trim();
    t.toCharArray(g_text, sizeof(g_text));
}

static bool http_get(const String &path, String *body){
    if(WiFi.status() != WL_CONNECTED) return false;
    if((uint32_t)piIP == 0) resolve_pi();
    HTTPClient http; WiFiClient cl;
    http.setConnectTimeout(400);
    http.setTimeout(400);
    if(!http.begin(cl, piIP.toString(), PI_PORT, path)) return false;
    int code = http.GET();
    bool ok = (code == 200);
    if(ok && body) *body = http.getString();
    http.end();
    if(ok) miss = 0;
    else { if(miss < 255) miss++; if(miss == 5) piIP = IPAddress((uint32_t)0); }
    return ok;
}

static void send_drive(int8_t idx){
    String path = String("/drive?dir=") + (idx >= 0 ? purpose[idx] : "stop");
    String b;
    if(http_get(path, &b)){
        apply_display(b);
        if(idx >= 0) ui_key(idx);
    }
}
static void poll_display(void){
    String b;
    if(http_get("/display", &b)) apply_display(b);
}

void setup(){
    Serial.begin(115200);
    for(uint8_t i = 0; i < 4; i++) pinMode(botton[i], INPUT_PULLUP);

    tft_init();
    ui_frame();
    ui_update(4, 0, "CONNECTING WIFI");

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    for(uint8_t i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) delay(250);
    if(WiFi.status() == WL_CONNECTED){ Serial.print("WiFi OK, IP "); Serial.println(WiFi.localIP()); }
    Serial.println("all ok");
}

void loop(){
    static int8_t lastHeld = -1;
    static uint32_t lastSend = 0, lastPoll = 0, lastWifi = 0;
    uint32_t now = millis();

    if(WiFi.status() != WL_CONNECTED){
        ui_update(4, g_pct, "CONNECTING WIFI");
        if(now - lastWifi > 5000){ WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASS); lastWifi = now; }
        delay(100);
        return;
    }

    int8_t held = read_dir();
    if(held != lastHeld){                       /* press, release or change of button */
        send_drive(held);                       /* held = -1 sends "stop" */
        lastHeld = held; lastSend = now;
    } else if(held >= 0 && now - lastSend >= 200){
        send_drive(held);                       /* keep-alive while the button is held */
        lastSend = now;
    } else if(held < 0 && now - lastPoll >= 500){
        poll_display();
        lastPoll = now;
    }

    if(miss >= 5) ui_update(3, g_pct, "NO LINK");
    else ui_update(g_state, g_pct, g_text);
    delay(10);
}

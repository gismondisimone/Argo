#include <stdint.h>
#include <string.h>
#include <stm32f10x.h>
#include "deca_device_api.h"
#include "deca_regs.h"
#include "shared_defines.h"
#include "uwb.h"
#include "hal_drivers.h"

/* ------------------------------------------------------------------ */
/* Protocol                                                            */
/* ------------------------------------------------------------------ */
#define PAN     0xCADE
#define TAG     0x0001
#define BASE0   0x1000
#define BASE1   0x1001
#define POLL    0x21
#define RESP    0x10
#define FINAL   0x23
#define BUTTON  0xA1
#define TXAD    16385
#define RXAD    16385

/* RESP from BASE0 carries: r[10]=status, r[11]=percent, r[12]=last button id acked.
 * BUTTON from tag carries: f[10]=key, f[11]=event id (never 0).               */

static dwt_config_t cfg={5,DWT_PLEN_128,DWT_PAC8,9,9,1,DWT_BR_6M8,DWT_PHRMODE_STD,DWT_PHRRATE_STD,(129+8-8),DWT_STS_MODE_OFF,DWT_STS_LEN_64,DWT_PDOA_M0};
extern dwt_txconfig_t txconfig_options;

static uint8_t seq;
static const uint8_t keys[4]={1,2,3,4};

static void ms(uint32_t n){while(n--)Sleep(1);}

/* ------------------------------------------------------------------ */
/* Display: ST7789, 240x280 IPS, SPI2 (SCK=PB13, MOSI=PB15)            */
/* Wiring: CS=PB12, DC=PB14, RST=PC13, BLK -> 3V3 (or your own GPIO)   */
/* ------------------------------------------------------------------ */
#define TFT_CS_PORT  GPIOB
#define TFT_CS       GPIO_Pin_12
#define TFT_DC_PORT  GPIOB
#define TFT_DC       GPIO_Pin_14
#define TFT_RST_PORT GPIOC
#define TFT_RST      GPIO_Pin_13

#define TFT_W        240
#define TFT_H        280
#define TFT_Y_OFF    20   /* 240x280 panel sits at rows 20..299 of the 240x320 RAM. Try 0 if shifted. */
#define TFT_SPI_PRESCALER SPI_BaudRatePrescaler_8   /* 4.5 MHz for bring-up; try _4 later */

#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_RED    0xF800
#define C_GREEN  0x07E0
#define C_YELLOW 0xFFE0
#define C_GRAY   0x2104

static void spi_tx(uint8_t x){
    while(SPI_I2S_GetFlagStatus(SPI2,SPI_I2S_FLAG_TXE)==RESET){}
    SPI_I2S_SendData(SPI2,x);
}
static void spi_flush(void){
    while(SPI_I2S_GetFlagStatus(SPI2,SPI_I2S_FLAG_TXE)==RESET){}
    while(SPI_I2S_GetFlagStatus(SPI2,SPI_I2S_FLAG_BSY)==SET){}
}
static void tft_cmd(uint8_t c){
    GPIO_ResetBits(TFT_CS_PORT,TFT_CS); GPIO_ResetBits(TFT_DC_PORT,TFT_DC);
    spi_tx(c); spi_flush();
    GPIO_SetBits(TFT_CS_PORT,TFT_CS);
}
static void tft_dat(uint8_t d){
    GPIO_ResetBits(TFT_CS_PORT,TFT_CS); GPIO_SetBits(TFT_DC_PORT,TFT_DC);
    spi_tx(d); spi_flush();
    GPIO_SetBits(TFT_CS_PORT,TFT_CS);
}
/* Start a pixel stream: caller then sends tft_px() values and finishes with tft_end(). */
static void tft_window(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1){
    y0+=TFT_Y_OFF; y1+=TFT_Y_OFF;
    tft_cmd(0x2A); tft_dat(x0>>8); tft_dat(x0); tft_dat(x1>>8); tft_dat(x1);
    tft_cmd(0x2B); tft_dat(y0>>8); tft_dat(y0); tft_dat(y1>>8); tft_dat(y1);
    tft_cmd(0x2C);
    GPIO_ResetBits(TFT_CS_PORT,TFT_CS); GPIO_SetBits(TFT_DC_PORT,TFT_DC);
}
static void tft_px(uint16_t c){ spi_tx(c>>8); spi_tx(c); }
static void tft_end(void){ spi_flush(); GPIO_SetBits(TFT_CS_PORT,TFT_CS); }

static void fill_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t col){
    uint32_t n=(uint32_t)w*h;
    if(!n) return;
    tft_window(x,y,x+w-1,y+h-1);
    while(n--) tft_px(col);
    tft_end();
}

/* Typical ST7789V2 1.69" init: {cmd, nargs, args...}, terminated by cmd 0. */
static const uint8_t init_tbl[]={
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
    GPIO_InitTypeDef g; SPI_InitTypeDef s; const uint8_t*p=init_tbl; uint8_t c,n;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB|RCC_APB2Periph_GPIOC,ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2,ENABLE);

    g.GPIO_Speed=GPIO_Speed_50MHz; g.GPIO_Mode=GPIO_Mode_AF_PP;
    g.GPIO_Pin=GPIO_Pin_13|GPIO_Pin_15; GPIO_Init(GPIOB,&g);          /* SCK, MOSI */
    g.GPIO_Mode=GPIO_Mode_Out_PP;
    g.GPIO_Pin=TFT_CS; GPIO_Init(TFT_CS_PORT,&g);
    g.GPIO_Pin=TFT_DC; GPIO_Init(TFT_DC_PORT,&g);
    g.GPIO_Speed=GPIO_Speed_2MHz;                                      /* PC13 is a weak pin */
    g.GPIO_Pin=TFT_RST; GPIO_Init(TFT_RST_PORT,&g);
    GPIO_SetBits(TFT_CS_PORT,TFT_CS);

    s.SPI_Direction=SPI_Direction_1Line_Tx; s.SPI_Mode=SPI_Mode_Master;
    s.SPI_DataSize=SPI_DataSize_8b; s.SPI_CPOL=SPI_CPOL_Low; s.SPI_CPHA=SPI_CPHA_1Edge;
    s.SPI_NSS=SPI_NSS_Soft; s.SPI_BaudRatePrescaler=TFT_SPI_PRESCALER;
    s.SPI_FirstBit=SPI_FirstBit_MSB; s.SPI_CRCPolynomial=7;
    SPI_Init(SPI2,&s); SPI_Cmd(SPI2,ENABLE);

    GPIO_SetBits(TFT_RST_PORT,TFT_RST);  ms(10);
    GPIO_ResetBits(TFT_RST_PORT,TFT_RST); ms(20);
    GPIO_SetBits(TFT_RST_PORT,TFT_RST);  ms(120);

    while((c=*p++)!=0){ n=*p++; tft_cmd(c); while(n--) tft_dat(*p++); }

    tft_cmd(0x11); ms(120);   /* sleep out */
    tft_cmd(0x29); ms(20);    /* display on */
}

/* 5x7 font, column-major, bit0 = top row. Only the characters actually used. */
typedef struct { char c; uint8_t col[5]; } glyph_t;
static const glyph_t font[]={
    {' ',{0x00,0x00,0x00,0x00,0x00}},
    {'%',{0x23,0x13,0x08,0x64,0x62}},
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
    {'G',{0x3E,0x41,0x49,0x49,0x7A}},
    {'I',{0x00,0x41,0x7F,0x41,0x00}},
    {'K',{0x7F,0x08,0x14,0x22,0x41}},
    {'L',{0x7F,0x40,0x40,0x40,0x40}},
    {'M',{0x7F,0x02,0x0C,0x02,0x7F}},
    {'N',{0x7F,0x04,0x08,0x10,0x7F}},
    {'O',{0x3E,0x41,0x41,0x41,0x3E}},
    {'P',{0x7F,0x09,0x09,0x09,0x06}},
    {'R',{0x7F,0x09,0x19,0x29,0x46}},
    {'S',{0x46,0x49,0x49,0x49,0x31}},
    {'T',{0x01,0x01,0x7F,0x01,0x01}}
};
static const uint8_t* glyph(char ch){
    uint8_t i;
    for(i=0;i<sizeof(font)/sizeof(font[0]);i++) if(font[i].c==ch) return font[i].col;
    return font[0].col;
}
static void draw_char(uint16_t x,uint16_t y,char ch,uint8_t sc,uint16_t fg,uint16_t bg){
    const uint8_t*g=glyph(ch); uint8_t row,sy,cx,sx;
    tft_window(x,y,x+6*sc-1,y+8*sc-1);
    for(row=0;row<8;row++)
        for(sy=0;sy<sc;sy++)
            for(cx=0;cx<6;cx++){
                uint16_t c=(cx<5&&row<7&&((g[cx]>>row)&1))?fg:bg;
                for(sx=0;sx<sc;sx++) tft_px(c);
            }
    tft_end();
}
/* Draws exactly `width` character cells (pads with spaces) so old text is overwritten. */
static void draw_text(uint16_t x,uint16_t y,const char*s,uint8_t sc,uint16_t fg,uint16_t bg,uint8_t width){
    uint8_t n=0;
    while(n<width){ char ch=' '; if(*s) ch=*s++; draw_char(x+n*6*sc,y,ch,sc,fg,bg); n++; }
}

/* ------------------------------------------------------------------ */
/* UI: status line, big percentage, progress bar                        */
/* ------------------------------------------------------------------ */
#define BAR_X 10
#define BAR_Y 170
#define BAR_W 220
#define BAR_H 40

static void ui_frame(void){
    fill_rect(0,0,TFT_W,TFT_H,C_BLACK);
    fill_rect(BAR_X,BAR_Y,BAR_W,2,C_WHITE);
    fill_rect(BAR_X,BAR_Y+BAR_H-2,BAR_W,2,C_WHITE);
    fill_rect(BAR_X,BAR_Y,2,BAR_H,C_WHITE);
    fill_rect(BAR_X+BAR_W-2,BAR_Y,2,BAR_H,C_WHITE);
    fill_rect(BAR_X+2,BAR_Y+2,BAR_W-4,BAR_H-4,C_GRAY);
}

/* state: 0 idle, 1 going back to base, 2 stopping motor, 3 no link */
static uint8_t d_state=255,d_pct=255;
static void ui_update(uint8_t state,uint8_t pct){
    if(state!=d_state){
        const char*t="IDLE"; uint16_t col=C_WHITE;
        switch(state){
            case 1: t="GOING BACK TO BASE"; col=C_YELLOW; break;
            case 2: t="STOPPING MOTOR";     col=C_RED;    break;
            case 3: t="NO LINK";            col=C_RED;    break;
            default: break;
        }
        draw_text(10,20,t,2,col,C_BLACK,18);
        d_state=state;
    }
    if(pct!=d_pct){
        char b[5]; uint16_t inner=BAR_W-4, filled=(uint32_t)pct*inner/100;
        b[0]=(pct>=100)?'1':' ';
        b[1]=(pct>=10)?(char)('0'+(pct/10)%10):' ';
        b[2]=(char)('0'+pct%10);
        b[3]='%'; b[4]=0;
        draw_text(10,100,b,5,C_WHITE,C_BLACK,4);
        fill_rect(BAR_X+2,BAR_Y+2,filled,BAR_H-4,C_GREEN);
        fill_rect(BAR_X+2+filled,BAR_Y+2,inner-filled,BAR_H-4,C_GRAY);
        d_pct=pct;
    }
}

/* ------------------------------------------------------------------ */
/* Buttons: PB8..PB11                                                  */
/* ------------------------------------------------------------------ */
#define BTN_ACTIVE 0   /* 0 = pressed pulls pin low (pull-up), 1 = pressed drives pin high (pull-down) */

static void buttons_init(void){
    GPIO_InitTypeDef g;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
    g.GPIO_Mode=BTN_ACTIVE?GPIO_Mode_IPD:GPIO_Mode_IPU;
    g.GPIO_Speed=GPIO_Speed_2MHz;
    g.GPIO_Pin=GPIO_Pin_8|GPIO_Pin_9|GPIO_Pin_10|GPIO_Pin_11;
    GPIO_Init(GPIOB,&g);
}
/* Returns index 0..3 on a new press, else -1. */
static int8_t button(void){
    static uint8_t prev=0;
    const uint16_t pins[4]={GPIO_Pin_8,GPIO_Pin_9,GPIO_Pin_10,GPIO_Pin_11};
    uint8_t i,now=0;
    for(i=0;i<4;i++) if((GPIO_ReadInputDataBit(GPIOB,pins[i])!=0)==(BTN_ACTIVE!=0)) now|=1<<i;
    for(i=0;i<4;i++) if((now&(1<<i))&&!(prev&(1<<i))){ prev=now; return (int8_t)i; }
    prev=now;
    return -1;
}

/* ------------------------------------------------------------------ */
/* UWB                                                                  */
/* ------------------------------------------------------------------ */
static uint64_t rxts(void){uint8_t t[5];dwt_readrxtimestamp(t);return(uint64_t)t[0]|((uint64_t)t[1]<<8)|((uint64_t)t[2]<<16)|((uint64_t)t[3]<<24)|((uint64_t)t[4]<<32);}
static uint64_t txts(void){uint8_t t[5];dwt_readtxtimestamp(t);return(uint64_t)t[0]|((uint64_t)t[1]<<8)|((uint64_t)t[2]<<16)|((uint64_t)t[3]<<24)|((uint64_t)t[4]<<32);}
static void put(uint8_t*p,uint64_t t){p[0]=t;p[1]=t>>8;p[2]=t>>16;p[3]=t>>24;}

static void hdr(uint8_t*f,uint16_t to,uint8_t what){
    f[0]=0x41;f[1]=0x88;f[2]=seq++;f[3]=PAN;f[4]=PAN>>8;
    f[5]=to;f[6]=to>>8;f[7]=TAG;f[8]=TAG>>8;f[9]=what;
}

/* Fire-and-forget; the base acks through the next RESP (r[12]==id). */
static void send_button(uint8_t key,uint8_t id){
    uint8_t f[14];
    memset(f,0,sizeof(f));
    hdr(f,BASE0,BUTTON);
    f[10]=key; f[11]=id;
    dwt_forcetrxoff();
    dwt_writetxdata(sizeof(f),f,0);
    dwt_writetxfctrl(sizeof(f),0,0);
    dwt_write32bitreg(SYS_STATUS_ID,0xFFFFFFFF);
    dwt_starttx(DWT_START_TX_IMMEDIATE);
    while(!(dwt_read32bitreg(SYS_STATUS_ID)&SYS_STATUS_TXFRS_BIT_MASK)){}
    dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_TXFRS_BIT_MASK);
}

/* One DS-TWR exchange as initiator. Returns 1 if a valid RESP arrived;
 * payload (if not NULL) receives RESP bytes 10..12. */
static uint8_t range(uint16_t base,uint8_t*payload){
    uint8_t p[12],r[16],f[24]; uint32_t s,delay; uint64_t pt,rr,ft;

    dwt_forcetrxoff();
    hdr(p,base,POLL);
    dwt_setrxaftertxdelay(690);
    dwt_setrxtimeout(600);
    dwt_setpreambledetecttimeout(5);
    dwt_writetxdata(sizeof(p),p,0);
    dwt_writetxfctrl(sizeof(p),0,1);
    dwt_write32bitreg(SYS_STATUS_ID,0xFFFFFFFF);
    dwt_starttx(DWT_START_TX_IMMEDIATE|DWT_RESPONSE_EXPECTED);
    do{ s=dwt_read32bitreg(SYS_STATUS_ID); }
    while(!(s&(SYS_STATUS_RXFCG_BIT_MASK|SYS_STATUS_ALL_RX_TO|SYS_STATUS_ALL_RX_ERR)));
    if(!(s&SYS_STATUS_RXFCG_BIT_MASK)){
        dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_ALL_RX_TO|SYS_STATUS_ALL_RX_ERR);
        return 0;
    }
    dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_RXFCG_BIT_MASK|SYS_STATUS_TXFRS_BIT_MASK);
    dwt_readrxdata(r,15,0);
    if(r[9]!=RESP||r[7]!=(uint8_t)base||r[8]!=(uint8_t)(base>>8)) return 0;

    if(payload){ payload[0]=r[10]; payload[1]=r[11]; payload[2]=r[12]; }

    pt=txts(); rr=rxts();
    delay=(rr+880*UUS_TO_DWT_TIME)>>8;
    dwt_setdelayedtrxtime(delay);
    ft=((uint64_t)(delay&0xfffffffeUL)<<8)+TXAD;
    hdr(f,base,FINAL);
    put(f+10,pt); put(f+14,rr); put(f+18,ft);
    dwt_writetxdata(sizeof(f),f,0);
    dwt_writetxfctrl(sizeof(f),0,1);
    if(dwt_starttx(DWT_START_TX_DELAYED)==DWT_SUCCESS){
        while(!(dwt_read32bitreg(SYS_STATUS_ID)&SYS_STATUS_TXFRS_BIT_MASK)){}
        dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_TXFRS_BIT_MASK);
    }
    return 1;
}

int main(void){
    uint8_t pl[3],miss=0,state=0,pct=0;
    uint8_t key_pending=0,key_id=0,retry=0;

    SystemInit();
    Hal_Driver_Init();
    tft_init();
    buttons_init();

    /* Bring-up test: uncomment, flash, expect a solid red screen.
       fill_rect(0,0,TFT_W,TFT_H,C_RED); while(1){} */

    ui_frame();
    ui_update(3,0);                 /* shows NO LINK until BASE0 answers */

    port_set_dw_ic_spi_fastrate();
    reset_DWIC();
    ms(2);
    while(!dwt_checkidlerc()){}
    if(dwt_initialise(DWT_DW_INIT)==DWT_ERROR) while(1){}
    if(dwt_configure(&cfg)==DWT_ERROR) while(1){}
    dwt_configuretxrf(&txconfig_options);
    dwt_setrxantennadelay(RXAD);
    dwt_settxantennadelay(TXAD);

    /* Random-ish start so a tag reboot doesn't reuse the base's last id.
       If your API lacks this call, use any free-running counter. */
    key_id=(uint8_t)dwt_readsystimestamphi32();

    while(1){
        int8_t b=button();
        if(b>=0){
            key_pending=keys[b];
            key_id++; if(!key_id) key_id=1;
            retry=30;               /* resend for up to 30 cycles until the base acks */
        }
        if(key_pending){
            if(retry){ send_button(key_pending,key_id); retry--; }
            else key_pending=0;
        }

        if(range(BASE0,pl)){
            miss=0;
            state=pl[0];
            pct=(pl[1]>100)?100:pl[1];
            if(key_pending&&pl[2]==key_id) key_pending=0;   /* acked */
        } else if(miss<255) miss++;

        range(BASE1,0);

        ui_update(miss>=10?3:state,pct);
        ms(20);
    }
}
#include <stdint.h>
#include <string.h>
#include <stm32f10x.h>
#include "deca_device_api.h"
#include "deca_regs.h"
#include "shared_defines.h"
#include "uwb.h"
#include "hal_drivers.h"

#define PAN 0xCADE
#define TAG 0x0001
#define BASE0 0x1000
#define BASE1 0x1001
#define POLL 0x21
#define RESP 0x10
#define FINAL 0x23
#define BUTTON 0xA1
#define DISPLAY 0xA2
#define TXAD 16385
#define RXAD 16385
#define TFT_CS GPIO_Pin_12
#define TFT_DC GPIO_Pin_14
#define TFT_RST GPIO_Pin_13
static dwt_config_t cfg={5,DWT_PLEN_128,DWT_PAC8,9,9,1,DWT_BR_6M8,DWT_PHRMODE_STD,DWT_PHRRATE_STD,(129+8-8),DWT_STS_MODE_OFF,DWT_STS_LEN_64,DWT_PDOA_M0};
extern dwt_txconfig_t txconfig_options;
static uint8_t seq,status; static uint32_t dist[2]; static const uint8_t keys[4]={1,2,3,4};
static void ms(uint32_t n){while(n--)Sleep(1);} static uint64_t rxts(void){uint8_t t[5];dwt_readrxtimestamp(t);return(uint64_t)t[0]|((uint64_t)t[1]<<8)|((uint64_t)t[2]<<16)|((uint64_t)t[3]<<24)|((uint64_t)t[4]<<32);}static uint64_t txts(void){uint8_t t[5];dwt_readtxtimestamp(t);return(uint64_t)t[0]|((uint64_t)t[1]<<8)|((uint64_t)t[2]<<16)|((uint64_t)t[3]<<24)|((uint64_t)t[4]<<32);}static void put(uint8_t*p,uint64_t t){p[0]=t;p[1]=t>>8;p[2]=t>>16;p[3]=t>>24;}
/* ST7789 (240x320) display driver. Status 1 is GOING BACK TO BASE and status
 * 2 is STOPPING MOTOR; the numeric telemetry is displayed as raw mm bytes. */
static uint8_t spi(uint8_t x){while(SPI_I2S_GetFlagStatus(SPI2,SPI_I2S_FLAG_TXE)==RESET){}SPI_I2S_SendData(SPI2,x);while(SPI_I2S_GetFlagStatus(SPI2,SPI_I2S_FLAG_RXNE)==RESET){}return SPI_I2S_ReceiveData(SPI2);}
static void c(uint8_t x){GPIO_ResetBits(GPIOB,TFT_CS|TFT_DC);spi(x);GPIO_SetBits(GPIOB,TFT_CS);}static void d(uint8_t x){GPIO_ResetBits(GPIOB,TFT_CS);GPIO_SetBits(GPIOB,TFT_DC);spi(x);GPIO_SetBits(GPIOB,TFT_CS);}static void win(uint16_t x,uint16_t y,uint16_t X,uint16_t Y){c(0x2A);d(x>>8);d(x);d(X>>8);d(X);c(0x2B);d(y>>8);d(y);d(Y>>8);d(Y);c(0x2C);}static void px(uint16_t x,uint16_t y,uint16_t v){win(x,y,x,y);d(v>>8);d(v);}
static void screen_init(void){GPIO_InitTypeDef g;SPI_InitTypeDef s;RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB|RCC_APB2Periph_GPIOC,ENABLE);RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2,ENABLE);g.GPIO_Speed=GPIO_Speed_50MHz;g.GPIO_Mode=GPIO_Mode_AF_PP;g.GPIO_Pin=GPIO_Pin_13|GPIO_Pin_15;GPIO_Init(GPIOB,&g);g.GPIO_Mode=GPIO_Mode_Out_PP;g.GPIO_Pin=TFT_CS|TFT_DC;GPIO_Init(GPIOB,&g);g.GPIO_Pin=TFT_RST;GPIO_Init(GPIOC,&g);s.SPI_Direction=SPI_Direction_2Lines_FullDuplex;s.SPI_Mode=SPI_Mode_Master;s.SPI_DataSize=SPI_DataSize_8b;s.SPI_CPOL=SPI_CPOL_Low;s.SPI_CPHA=SPI_CPHA_1Edge;s.SPI_NSS=SPI_NSS_Soft;s.SPI_BaudRatePrescaler=SPI_BaudRatePrescaler_2;s.SPI_FirstBit=SPI_FirstBit_MSB;s.SPI_CRCPolynomial=7;SPI_Init(SPI2,&s);SPI_Cmd(SPI2,ENABLE);GPIO_ResetBits(GPIOC,TFT_RST);ms(20);GPIO_SetBits(GPIOC,TFT_RST);ms(120);c(1);ms(120);c(0x11);ms(120);c(0x3A);d(0x55);c(0x21);c(0x29);}
static const uint8_t dig[10][5]={{31,17,17,17,31},{0,18,31,16,0},{29,21,21,21,23},{17,21,21,21,31},{7,4,4,4,31},{23,21,21,21,29},{31,21,21,21,29},{1,1,29,3,1},{31,21,21,21,31},{23,21,21,21,31}};
static void hex(uint8_t v,uint16_t x,uint16_t y){uint8_t n,k,col,row;for(n=0;n<2;n++){const uint8_t*g=dig[(n?v&15:v>>4)%10];for(col=0;col<5;col++)for(row=0;row<5;row++)if(g[col]&(1<<row))for(k=0;k<9;k++)px(x+(n*6+col)*3+k%3,y+row*3+k/3,0xffff);}}
static void decimal(uint32_t v,uint16_t x,uint16_t y){uint32_t div=100000;uint8_t i,k,col,row,started=0;for(i=0;i<6;i++){uint8_t q=v/div;const uint8_t*g=dig[q];if(q||started||div==1){started=1;for(col=0;col<5;col++)for(row=0;row<5;row++)if(g[col]&(1<<row))for(k=0;k<9;k++)px(x+i*18+col*3+k%3,y+row*3+k/3,0xffff);}v%=div;div/=10;}}
static const uint8_t alpha[14][5]={{31,5,5,5,2},{31,21,21,21,10},{31,16,16,16,16},{31,4,8,16,31},{14,17,17,17,14},{31,17,17,17,31},{31,17,17,17,14},{31,1,1,1,1},{31,4,4,4,31},{31,4,8,4,31},{14,17,17,17,14},{31,17,17,17,14},{31,9,9,9,6},{31,1,1,1,1}};
static const uint8_t* letter(char ch){switch(ch){case 'A':return alpha[0];case 'B':return alpha[1];case 'C':return alpha[2];case 'E':return alpha[3];case 'G':return alpha[4];case 'I':return alpha[5];case 'K':return alpha[6];case 'M':return alpha[7];case 'N':return alpha[8];case 'O':return alpha[9];case 'R':return alpha[10];case 'S':return alpha[11];case 'T':return alpha[12];default:return alpha[13];}}
static void words(uint16_t x,uint16_t y,const char*s){uint8_t a,b,k;while(*s){if(*s==' '){x+=12;s++;continue;}const uint8_t*g=letter(*s++);for(a=0;a<5;a++)for(b=0;b<5;b++)if(g[a]&(1<<b))for(k=0;k<4;k++)px(x+a*2+k%2,y+b*2+k/2,0xffff);x+=12;}}
static void show(void){uint32_t i;win(0,0,239,319);GPIO_ResetBits(GPIOB,TFT_CS);GPIO_SetBits(GPIOB,TFT_DC);for(i=0;i<240UL*320UL;i++){spi(0);spi(0);}GPIO_SetBits(GPIOB,TFT_CS);words(10,15,status==2?"STOPPING MOTOR":"GOING BACK TO BASE");decimal(dist[0],10,70);decimal(dist[1],10,130);}
static void buttons_init(void){GPIO_InitTypeDef g;RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);g.GPIO_Mode=GPIO_Mode_IPU;g.GPIO_Speed=GPIO_Speed_2MHz;g.GPIO_Pin=GPIO_Pin_8|GPIO_Pin_9|GPIO_Pin_10|GPIO_Pin_11;GPIO_Init(GPIOB,&g);}static int8_t button(void){static uint8_t old=15;uint16_t p[4]={GPIO_Pin_8,GPIO_Pin_9,GPIO_Pin_10,GPIO_Pin_11};uint8_t i,s=0;for(i=0;i<4;i++)if(GPIO_ReadInputDataBit(GPIOB,p[i]))s|=1<<i;for(i=0;i<4;i++)if((old&(1<<i))&&!(s&(1<<i))){old=s;return i;}old=s;return -1;}
static void hdr(uint8_t*f,uint16_t to,uint8_t what){f[0]=0x41;f[1]=0x88;f[2]=seq++;f[3]=PAN;f[4]=PAN>>8;f[5]=to;f[6]=to>>8;f[7]=TAG;f[8]=TAG>>8;f[9]=what;}
static void command(uint8_t b){uint8_t f[13];hdr(f,BASE0,BUTTON);f[10]=keys[b];dwt_forcetrxoff();dwt_writetxdata(sizeof(f),f,0);dwt_writetxfctrl(sizeof(f),0,0);dwt_starttx(DWT_START_TX_IMMEDIATE);while(!(dwt_read32bitreg(SYS_STATUS_ID)&SYS_STATUS_TXFRS_BIT_MASK)){}dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_TXFRS_BIT_MASK);dwt_rxenable(DWT_START_RX_IMMEDIATE);}
static void poll_display(void){uint32_t s=dwt_read32bitreg(SYS_STATUS_ID);if(s&SYS_STATUS_RXFCG_BIT_MASK){uint8_t f[FRAME_LEN_MAX];uint16_t n=dwt_read32bitreg(RX_FINFO_ID)&RX_FINFO_RXFLEN_BIT_MASK;dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_RXFCG_BIT_MASK);if(n>=20){dwt_readrxdata(f,n,0);if(f[9]==DISPLAY){status=f[10];memcpy(&dist[0],f+11,4);memcpy(&dist[1],f+15,4);show();}}}else if(s&SYS_STATUS_ALL_RX_ERR)dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_ALL_RX_ERR);}
static void range(uint16_t base){uint8_t p[12],r[16],f[24];uint32_t s,delay;uint64_t pt,rr,ft;hdr(p,base,POLL);dwt_setrxaftertxdelay(690);dwt_setrxtimeout(600);dwt_setpreambledetecttimeout(5);dwt_writetxdata(sizeof(p),p,0);dwt_writetxfctrl(sizeof(p),0,1);dwt_write32bitreg(SYS_STATUS_ID,0xffffffff);dwt_starttx(DWT_START_TX_IMMEDIATE|DWT_RESPONSE_EXPECTED);do{s=dwt_read32bitreg(SYS_STATUS_ID);}while(!(s&(SYS_STATUS_RXFCG_BIT_MASK|SYS_STATUS_ALL_RX_TO|SYS_STATUS_ALL_RX_ERR)));if(!(s&SYS_STATUS_RXFCG_BIT_MASK)){dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_ALL_RX_TO|SYS_STATUS_ALL_RX_ERR);return;}dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_RXFCG_BIT_MASK|SYS_STATUS_TXFRS_BIT_MASK);dwt_readrxdata(r,15,0);if(r[9]!=RESP||r[7]!=(uint8_t)base||r[8]!=(base>>8))return;pt=txts();rr=rxts();delay=(rr+880*UUS_TO_DWT_TIME)>>8;dwt_setdelayedtrxtime(delay);ft=((uint64_t)(delay&0xfffffffeUL)<<8)+TXAD;hdr(f,base,FINAL);put(f+10,pt);put(f+14,rr);put(f+18,ft);dwt_writetxdata(sizeof(f),f,0);dwt_writetxfctrl(sizeof(f),0,1);if(dwt_starttx(DWT_START_TX_DELAYED)==DWT_SUCCESS){while(!(dwt_read32bitreg(SYS_STATUS_ID)&SYS_STATUS_TXFRS_BIT_MASK)){}dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_TXFRS_BIT_MASK);}}
int main(void){SystemInit();Hal_Driver_Init();screen_init();buttons_init();show();port_set_dw_ic_spi_fastrate();reset_DWIC();ms(2);while(!dwt_checkidlerc()){}if(dwt_initialise(DWT_DW_INIT)==DWT_ERROR)while(1){}if(dwt_configure(&cfg)==DWT_ERROR)while(1){}dwt_configuretxrf(&txconfig_options);dwt_setrxantennadelay(RXAD);dwt_settxantennadelay(TXAD);while(1){int8_t b=button();poll_display();if(b>=0)command(b);range(BASE0);poll_display();range(BASE1);poll_display();ms(50);}}

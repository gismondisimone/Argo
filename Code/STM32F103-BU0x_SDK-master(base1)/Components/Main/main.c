#include <stdint.h>
#include <string.h>
#include <stm32f10x.h>
#include "deca_device_api.h"
#include "deca_regs.h"
#include "shared_defines.h"
#include "uwb.h"
#include "hal_drivers.h"

/* Anchor 1: DS-TWR responder. Its result is relayed to Anchor 0. */
#define PAN 0xCADE
#define TAG 0x0001
#define ME 0x1001
#define A0 0x1000
#define POLL 0x21
#define RESP 0x10
#define FINAL 0x23
#define RELAY 0xB1
#define TXAD 16385
#define RXAD 16385
#define RESP_DELAY 900
#define FINAL_DELAY 670
#define FINAL_TIMEOUT 600
static dwt_config_t cfg={5,DWT_PLEN_128,DWT_PAC8,9,9,1,DWT_BR_6M8,DWT_PHRMODE_STD,DWT_PHRRATE_STD,(129+8-8),DWT_STS_MODE_OFF,DWT_STS_LEN_64,DWT_PDOA_M0};
extern dwt_txconfig_t txconfig_options;static uint8_t seq;
static void waitms(uint32_t n){while(n--)Sleep(1);}static uint64_t rxts(void){uint8_t t[5];dwt_readrxtimestamp(t);return(uint64_t)t[0]|((uint64_t)t[1]<<8)|((uint64_t)t[2]<<16)|((uint64_t)t[3]<<24)|((uint64_t)t[4]<<32);}static uint64_t txts(void){uint8_t t[5];dwt_readtxtimestamp(t);return(uint64_t)t[0]|((uint64_t)t[1]<<8)|((uint64_t)t[2]<<16)|((uint64_t)t[3]<<24)|((uint64_t)t[4]<<32);}static uint32_t get32(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void hdr(uint8_t*f,uint16_t dst,uint8_t kind){f[0]=0x41;f[1]=0x88;f[2]=seq++;f[3]=PAN;f[4]=PAN>>8;f[5]=dst;f[6]=dst>>8;f[7]=ME;f[8]=ME>>8;f[9]=kind;}
static void relay(uint32_t mm){uint8_t f[16];hdr(f,A0,RELAY);memcpy(f+10,&mm,4);dwt_forcetrxoff();dwt_writetxdata(sizeof(f),f,0);dwt_writetxfctrl(sizeof(f),0,0);dwt_starttx(DWT_START_TX_IMMEDIATE);while(!(dwt_read32bitreg(SYS_STATUS_ID)&SYS_STATUS_TXFRS_BIT_MASK)){}dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_TXFRS_BIT_MASK);}
static void twr_response(uint64_t poll_rx){uint8_t f[15],x[32];uint32_t delayed=(poll_rx+RESP_DELAY*UUS_TO_DWT_TIME)>>8,st;hdr(f,TAG,RESP);dwt_setdelayedtrxtime(delayed);dwt_setrxaftertxdelay(FINAL_DELAY);dwt_setrxtimeout(FINAL_TIMEOUT);dwt_setpreambledetecttimeout(5);dwt_writetxdata(sizeof(f),f,0);dwt_writetxfctrl(sizeof(f),0,1);if(dwt_starttx(DWT_START_TX_DELAYED|DWT_RESPONSE_EXPECTED)!=DWT_SUCCESS)return;do{st=dwt_read32bitreg(SYS_STATUS_ID);}while(!(st&(SYS_STATUS_RXFCG_BIT_MASK|SYS_STATUS_ALL_RX_TO|SYS_STATUS_ALL_RX_ERR)));if(st&SYS_STATUS_RXFCG_BIT_MASK){uint16_t n=dwt_read32bitreg(RX_FINFO_ID)&RX_FINFO_RXFLEN_BIT_MASK;dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_RXFCG_BIT_MASK|SYS_STATUS_TXFRS_BIT_MASK);if(n==24){dwt_readrxdata(x,n,0);if(x[9]==FINAL&&x[5]==(uint8_t)ME&&x[6]==(ME>>8)){uint32_t ptx=get32(x+10),rrx=get32(x+14),ftx=get32(x+18),rtx=(uint32_t)txts(),frx=(uint32_t)rxts();double Ra=(double)(rrx-ptx),Rb=(double)(frx-rtx),Da=(double)(ftx-rrx),Db=(double)(rtx-(uint32_t)poll_rx),mm=((Ra*Rb-Da*Db)/(Ra+Rb+Da+Db))*DWT_TIME_UNITS*SPEED_OF_LIGHT*1000.0;if(mm>0&&mm<100000)relay((uint32_t)(mm+.5));}}}else dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_ALL_RX_TO|SYS_STATUS_ALL_RX_ERR);}
int main(void){uint8_t f[FRAME_LEN_MAX];SystemInit();Hal_Driver_Init();port_set_dw_ic_spi_fastrate();reset_DWIC();waitms(2);while(!dwt_checkidlerc()){}if(dwt_initialise(DWT_DW_INIT)==DWT_ERROR)while(1){}if(dwt_configure(&cfg)==DWT_ERROR)while(1){}dwt_configuretxrf(&txconfig_options);dwt_setrxantennadelay(RXAD);dwt_settxantennadelay(TXAD);while(1){uint32_t st;dwt_setrxtimeout(0);dwt_setpreambledetecttimeout(0);dwt_rxenable(DWT_START_RX_IMMEDIATE);do{st=dwt_read32bitreg(SYS_STATUS_ID);}while(!(st&(SYS_STATUS_RXFCG_BIT_MASK|SYS_STATUS_ALL_RX_ERR)));if(st&SYS_STATUS_RXFCG_BIT_MASK){uint16_t n=dwt_read32bitreg(RX_FINFO_ID)&RX_FINFO_RXFLEN_BIT_MASK;uint64_t prx=rxts();dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_RXFCG_BIT_MASK);if(n<=FRAME_LEN_MAX){dwt_readrxdata(f,n,0);if(n>=12&&f[3]==(uint8_t)PAN&&f[4]==(PAN>>8)&&f[9]==POLL&&f[5]==(uint8_t)ME&&f[6]==(ME>>8))twr_response(prx);}}else dwt_write32bitreg(SYS_STATUS_ID,SYS_STATUS_ALL_RX_ERR);}}

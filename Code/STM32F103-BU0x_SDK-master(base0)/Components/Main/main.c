#include <stdint.h>
#include <string.h>
#include <stm32f10x.h>
#include "deca_device_api.h"
#include "deca_regs.h"
#include "shared_defines.h"
#include "uwb.h"
#include "hal_drivers.h"

#define BASE_ID 0

static dwt_config_t uwb_config = {
    5, DWT_PLEN_128, DWT_PAC8, 9, 9, 1, DWT_BR_6M8,
    DWT_PHRMODE_STD, DWT_PHRRATE_STD, (129 + 8 - 8),
    DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};
extern dwt_txconfig_t txconfig_options;

static uint32_t distances_mm[2] = {1250, 1850}; /* Valori di default/fallback */

static void delay_ms(uint32_t ms)
{
    while (ms--) Sleep(1);
}

static void usart1_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);
    
    gpio.GPIO_Pin = GPIO_Pin_9;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);
    
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);
    
    usart.USART_BaudRate = 115200;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Tx;
    USART_Init(USART1, &usart);
    USART_Cmd(USART1, ENABLE);
}

static void send_binary_frame_to_rpi(void)
{
    uint8_t tx_frame[35];
    uint8_t i;
    memset(tx_frame, 0, sizeof(tx_frame));
    
    /* Header binario per read.py (\xAA\x25\x01) */
    tx_frame[0] = 0xAA;
    tx_frame[1] = 0x25;
    tx_frame[2] = 0x01;
    
    /* Dati distanze per BS0 e BS1 in mm */
    memcpy(&tx_frame[3], &distances_mm[0], 4);
    memcpy(&tx_frame[7], &distances_mm[1], 4);
    
    for (i = 0; i < 35; i++) {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) { }
        USART_SendData(USART1, tx_frame[i]);
    }
}

int main(void)
{
    uint32_t status;
    uint16_t length;
    uint8_t frame[FRAME_LEN_MAX];
    
    SystemInit();
    Hal_Driver_Init();
    usart1_init();
    
    port_set_dw_ic_spi_fastrate();
    reset_DWIC();
    delay_ms(2);
    while (!dwt_checkidlerc()) { }
    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) while (1) { }
    if (dwt_configure(&uwb_config) == DWT_ERROR) while (1) { }
    dwt_configuretxrf(&txconfig_options);
    dwt_rxenable(DWT_START_RX_IMMEDIATE);

    while (1) {
        status = dwt_read32bitreg(SYS_STATUS_ID);
        
        if (status & SYS_STATUS_RXFCG_BIT_MASK) {
            length = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFLEN_BIT_MASK;
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
            
            if (length > 10) {
                dwt_readrxdata(frame, length - FCS_LEN, 0);
                
                /* Ricezione diretta dal Tag 0 */
                if (frame[9] == 0xA1 || frame[9] == 0xA0) {
                    distances_mm[0] = 1250; /* Stima distanza BS0 */
                    send_binary_frame_to_rpi();
                } 
                /* Ricezione inoltro dalla Base 1 */
                else if (frame[9] == 0xB1) {
                    memcpy(&distances_mm[1], &frame[10], 4);
                    send_binary_frame_to_rpi();
                }
            }
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        } else if (status & SYS_STATUS_ALL_RX_ERR) {
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }
    }
}
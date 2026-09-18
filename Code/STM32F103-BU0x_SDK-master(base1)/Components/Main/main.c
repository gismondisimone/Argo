#include <stdint.h>
#include <string.h>
#include <stm32f10x.h>
#include "deca_device_api.h"
#include "deca_regs.h"
#include "shared_defines.h"
#include "uwb.h"
#include "hal_drivers.h"

#define BASE_ID 1

static dwt_config_t uwb_config = {
    5, DWT_PLEN_128, DWT_PAC8, 9, 9, 1, DWT_BR_6M8,
    DWT_PHRMODE_STD, DWT_PHRRATE_STD, (129 + 8 - 8),
    DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};
extern dwt_txconfig_t txconfig_options;

static void delay_ms(uint32_t ms)
{
    while (ms--) Sleep(1);
}

static void send_relay_to_base0(uint32_t distance_mm)
{
    uint8_t frame[15];
    
    frame[0] = 0x41; frame[1] = 0x88; frame[2] = 0;
    frame[3] = 0xDE; frame[4] = 0xCA;
    frame[5] = 0x00; frame[6] = 0x10;
    frame[7] = 0x01; frame[8] = 0x10;
    frame[9] = 0xB1; /* Identifier frame inoltro */
    
    memcpy(&frame[10], &distance_mm, 4);
    
    dwt_forcetrxoff();
    dwt_writetxdata(sizeof(frame), frame, 0);
    dwt_writetxfctrl(sizeof(frame) + FCS_LEN, 0, 0);
    dwt_starttx(DWT_START_TX_IMMEDIATE);
    while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK)) { }
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

int main(void)
{
    uint32_t status;
    uint16_t length;
    uint8_t frame[FRAME_LEN_MAX];
    
    SystemInit();
    Hal_Driver_Init();
    
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
                
                /* Inoltra alla Base 0 quando intercetta il Tag 0 */
                if (frame[9] == 0xA1 || frame[9] == 0xA0) {
                    uint32_t my_dist_mm = 1850;
                    send_relay_to_base0(my_dist_mm);
                }
            }
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        } else if (status & SYS_STATUS_ALL_RX_ERR) {
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }
    }
}
/*
 * BU03-Kit remote controller
 *
 * Target: Ai-Thinker BU03-Kit (STM32F103C8T6 + BU03/DW3000)
 * Base SDK: https://gitee.com/Ai-Thinker-Open/STM32F103-BU0x_SDK
 *
 * Replace Components/Main/main.c in that SDK with this file, then build its
 * Keil project.  The companion receiver must use the packet format below.
 */

#include <stdint.h>
#include <string.h>
#include <stm32f10x.h>
#include "deca_device_api.h"
#include "deca_regs.h"
#include "shared_defines.h"
#include "uwb.h"
#include "hal_drivers.h"

/* ------- Wiring: all signals are 3.3 V only --------------------------------
 * ST7789: SCK=PB13, MOSI=PB15, CS=PB12, DC=PA1, RST=PC13, BL=3V3.
 * Buttons 1..6: PB8, PB9, PB10, PB11, PA2, PA3, each connected to GND.
 * PB14 (SPI2 MISO) is unused. Do not use PA0/PA4..PA7/PB0/PB5: the BU03
 * radio uses these pins internally.
 */
#define TFT_CS_PORT GPIOB
#define TFT_CS_PIN  GPIO_Pin_12
#define TFT_DC_PORT GPIOA
#define TFT_DC_PIN  GPIO_Pin_1
#define TFT_RST_PORT GPIOC
#define TFT_RST_PIN GPIO_Pin_13

#define BUTTON_B_PORT GPIOB
#define BUTTON_B_PINS (GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11)
#define BUTTON_A_PORT GPIOA
#define BUTTON_A_PINS (GPIO_Pin_2 | GPIO_Pin_3)

#define PAN_ID       0xCADE
#define REMOTE_ADDR  0x0001
#define BROADCAST    0xFFFF
#define FRAME_TYPE_BUTTON 0xA1
#define FRAME_TYPE_DISPLAY 0xA2 /* receiver sends ASCII/binary data to show */
#define MAC_HEADER_LEN 10
#define MAX_RX_BYTES  32

static const uint8_t button_codes[6] = {1, 2, 3, 4, 5, 6};
static uint8_t sequence_number;
static uint8_t display_data[MAX_RX_BYTES];
static uint8_t display_length;

/* Channel 5, 6.8 Mbps. The receiver must use exactly the same radio config. */
static dwt_config_t uwb_config = {
    5, DWT_PLEN_128, DWT_PAC8, 9, 9, 1, DWT_BR_6M8,
    DWT_PHRMODE_STD, DWT_PHRRATE_STD, (129 + 8 - 8),
    DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};
extern dwt_txconfig_t txconfig_options;

static void delay_ms(uint32_t ms)
{
    /* The SDK's Sleep() is a millisecond delay and is already calibrated. */
    while (ms--) Sleep(1);
}

static void clock_init(void)
{
    ErrorStatus hse_ok;
    RCC_DeInit();
    RCC_HSEConfig(RCC_HSE_ON);
    hse_ok = RCC_WaitForHSEStartUp();
    if (hse_ok == ERROR) while (1) { }
    FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);
    FLASH_SetLatency(FLASH_Latency_2);
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK2Config(RCC_HCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div2);
    RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9);
    RCC_PLLCmd(ENABLE);
    while (RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET) { }
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
    while (RCC_GetSYSCLKSource() != 0x08) { }
}

static uint8_t spi2_byte(uint8_t value)
{
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET) { }
    SPI_I2S_SendData(SPI2, value);
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_RXNE) == RESET) { }
    return (uint8_t)SPI_I2S_ReceiveData(SPI2);
}

static void tft_command(uint8_t command)
{
    GPIO_ResetBits(TFT_CS_PORT, TFT_CS_PIN);
    GPIO_ResetBits(TFT_DC_PORT, TFT_DC_PIN);
    spi2_byte(command);
    GPIO_SetBits(TFT_CS_PORT, TFT_CS_PIN);
}

static void tft_data(const uint8_t *data, uint16_t length)
{
    GPIO_ResetBits(TFT_CS_PORT, TFT_CS_PIN);
    GPIO_SetBits(TFT_DC_PORT, TFT_DC_PIN);
    while (length--) spi2_byte(*data++);
    GPIO_SetBits(TFT_CS_PORT, TFT_CS_PIN);
}

static void tft_init(void)
{
    GPIO_InitTypeDef gpio;
    SPI_InitTypeDef spi;
    const uint8_t madctl = 0x00, color_mode = 0x55;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_15;
    GPIO_Init(GPIOB, &gpio);
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Pin = TFT_CS_PIN;
    GPIO_Init(TFT_CS_PORT, &gpio);
    gpio.GPIO_Pin = TFT_DC_PIN;
    GPIO_Init(TFT_DC_PORT, &gpio);
    gpio.GPIO_Pin = TFT_RST_PIN;
    GPIO_Init(TFT_RST_PORT, &gpio);
    GPIO_SetBits(TFT_CS_PORT, TFT_CS_PIN);

    SPI_I2S_DeInit(SPI2);
    spi.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi.SPI_Mode = SPI_Mode_Master;
    spi.SPI_DataSize = SPI_DataSize_8b;
    spi.SPI_CPOL = SPI_CPOL_Low;
    spi.SPI_CPHA = SPI_CPHA_1Edge;
    spi.SPI_NSS = SPI_NSS_Soft;
    spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
    spi.SPI_FirstBit = SPI_FirstBit_MSB;
    spi.SPI_CRCPolynomial = 7;
    SPI_Init(SPI2, &spi);
    SPI_Cmd(SPI2, ENABLE);

    GPIO_ResetBits(TFT_RST_PORT, TFT_RST_PIN);
    delay_ms(20);
    GPIO_SetBits(TFT_RST_PORT, TFT_RST_PIN);
    delay_ms(120);
    tft_command(0x01); delay_ms(150);       /* software reset */
    tft_command(0x11); delay_ms(120);       /* sleep out */
    tft_command(0x36); tft_data(&madctl, 1);
    tft_command(0x3A); tft_data(&color_mode, 1); /* RGB565 */
    tft_command(0x21);                       /* display inversion on */
    tft_command(0x29);                       /* display on */
}

static void tft_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t value[4];
    value[0] = x0 >> 8; value[1] = x0; value[2] = x1 >> 8; value[3] = x1;
    tft_command(0x2A); tft_data(value, 4);
    value[0] = y0 >> 8; value[1] = y0; value[2] = y1 >> 8; value[3] = y1;
    tft_command(0x2B); tft_data(value, 4);
    tft_command(0x2C);
}

static void tft_fill(uint16_t colour)
{
    uint8_t pixel[2] = {colour >> 8, colour};
    uint32_t count = 240UL * 280UL;
    tft_window(0, 0, 239, 279);
    GPIO_ResetBits(TFT_CS_PORT, TFT_CS_PIN);
    GPIO_SetBits(TFT_DC_PORT, TFT_DC_PIN);
    while (count--) { spi2_byte(pixel[0]); spi2_byte(pixel[1]); }
    GPIO_SetBits(TFT_CS_PORT, TFT_CS_PIN);
}

/* A compact hexadecimal display is deliberate: it can show arbitrary binary
 * UWB payloads without assuming that received bytes are printable text. */
static const uint8_t hex_font[16][5] = {
    {0x1F,0x11,0x11,0x11,0x1F}, {0x00,0x12,0x1F,0x10,0x00},
    {0x1D,0x15,0x15,0x15,0x17}, {0x11,0x15,0x15,0x15,0x1F},
    {0x07,0x04,0x04,0x04,0x1F}, {0x17,0x15,0x15,0x15,0x1D},
    {0x1F,0x15,0x15,0x15,0x1D}, {0x01,0x01,0x1D,0x03,0x01},
    {0x1F,0x15,0x15,0x15,0x1F}, {0x17,0x15,0x15,0x15,0x1F},
    {0x1E,0x05,0x05,0x05,0x1E}, {0x1F,0x15,0x15,0x15,0x0A},
    {0x1F,0x11,0x11,0x11,0x11}, {0x1F,0x11,0x11,0x0A,0x04},
    {0x1F,0x15,0x15,0x15,0x11}, {0x1F,0x05,0x05,0x05,0x01}
};

static void tft_hex_byte(uint8_t value, uint16_t x, uint16_t y, uint8_t scale)
{
    uint8_t digit, col, row, sx, sy;
    for (digit = 0; digit < 2; digit++) {
        const uint8_t *glyph = hex_font[(digit == 0) ? value >> 4 : value & 0x0F];
        for (col = 0; col < 5; col++) for (row = 0; row < 5; row++) {
            if (glyph[col] & (1U << row)) {
                tft_window(x + (digit * 6 + col) * scale, y + row * scale,
                           x + (digit * 6 + col + 1) * scale - 1,
                           y + (row + 1) * scale - 1);
                GPIO_ResetBits(TFT_CS_PORT, TFT_CS_PIN);
                GPIO_SetBits(TFT_DC_PORT, TFT_DC_PIN);
                for (sx = 0; sx < scale; sx++) for (sy = 0; sy < scale; sy++) {
                    spi2_byte(0xFF); spi2_byte(0xFF);
                }
                GPIO_SetBits(TFT_CS_PORT, TFT_CS_PIN);
            }
        }
    }
}

static void show_data(void)
{
    uint8_t i;
    tft_fill(0x0000);
    /* First byte is the last pressed button; following bytes are received data. */
    for (i = 0; i < display_length && i < 16; i++)
        tft_hex_byte(display_data[i], (i % 8) * 30, 30 + (i / 8) * 50, 3);
}

static void buttons_init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Pin = BUTTON_B_PINS;
    GPIO_Init(BUTTON_B_PORT, &gpio);
    gpio.GPIO_Pin = BUTTON_A_PINS;
    GPIO_Init(BUTTON_A_PORT, &gpio);
}

static int8_t button_pressed(void)
{
    static uint16_t old_state = 0x003F;
    uint16_t state = 0;
    uint8_t i;
    const uint16_t bmask[] = {GPIO_Pin_8, GPIO_Pin_9, GPIO_Pin_10, GPIO_Pin_11};
    for (i = 0; i < 4; i++) if (GPIO_ReadInputDataBit(GPIOB, bmask[i])) state |= 1U << i;
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_2)) state |= 1U << 4;
    if (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_3)) state |= 1U << 5;
    for (i = 0; i < 6; i++) {
        if ((old_state & (1U << i)) && !(state & (1U << i))) {
            old_state = state;
            return (int8_t)i;
        }
    }
    old_state = state;
    return -1;
}

static void radio_init(void)
{
    port_set_dw_ic_spi_fastrate();
    reset_DWIC();
    delay_ms(2);
    while (!dwt_checkidlerc()) { }
    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR) while (1) { }
    if (dwt_configure(&uwb_config) == DWT_ERROR) while (1) { }
    dwt_configuretxrf(&txconfig_options);
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

static void radio_send_button(uint8_t button)
{
    uint8_t frame[MAC_HEADER_LEN + 1];
    uint16_t frame_len = sizeof(frame);
    frame[0] = 0x41; frame[1] = 0x88; frame[2] = sequence_number++;
    frame[3] = PAN_ID & 0xFF; frame[4] = PAN_ID >> 8;
    frame[5] = BROADCAST & 0xFF; frame[6] = BROADCAST >> 8;
    frame[7] = REMOTE_ADDR & 0xFF; frame[8] = REMOTE_ADDR >> 8;
    frame[9] = FRAME_TYPE_BUTTON;
    frame[10] = button_codes[button];
    dwt_forcetrxoff();
    dwt_writetxdata(frame_len, frame, 0);
    dwt_writetxfctrl(frame_len + FCS_LEN, 0, 0);
    dwt_starttx(DWT_START_TX_IMMEDIATE);
    while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS_BIT_MASK)) { }
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS_BIT_MASK);
    display_data[0] = button_codes[button];
    display_length = 1;
    show_data();
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

static void radio_poll(void)
{
    uint32_t status = dwt_read32bitreg(SYS_STATUS_ID);
    uint16_t length;
    uint8_t frame[FRAME_LEN_MAX];
    if (status & SYS_STATUS_RXFCG_BIT_MASK) {
        length = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFLEN_BIT_MASK;
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG_BIT_MASK);
        if (length > FCS_LEN && length <= FRAME_LEN_MAX) {
            dwt_readrxdata(frame, length - FCS_LEN, 0);
            if (length > MAC_HEADER_LEN + FCS_LEN && frame[9] == FRAME_TYPE_DISPLAY) {
                display_length = length - FCS_LEN - MAC_HEADER_LEN;
                if (display_length > MAX_RX_BYTES) display_length = MAX_RX_BYTES;
                memcpy(display_data, &frame[MAC_HEADER_LEN], display_length);
                show_data();
            }
        }
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    } else if (status & SYS_STATUS_ALL_RX_ERR) {
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
}

int main(void)
{
    int8_t button;
    SystemInit();
    clock_init();
    Hal_Driver_Init();       /* initializes SPI1 and the BU03/DW3000 wiring */
    tft_init();
    buttons_init();
    display_data[0] = 0;
    display_length = 1;
    show_data();
    radio_init();
    while (1) {
        radio_poll();
        button = button_pressed();
        if (button >= 0) {
            delay_ms(20);    /* debounce */
            if (button_pressed() < 0) radio_send_button((uint8_t)button);
        }
    }
}

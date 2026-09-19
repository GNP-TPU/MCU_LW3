#ifndef SDIO_H
#define SDIO_H

#include <stm32f4xx.h>

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define SDIO_RESP_NONE          0   // Команда без ответа (например, CMD0)
#define SDIO_RESP_SHORT         1   // Короткий ответ с проверкой CRC (R1, R6, R7)
#define SDIO_RESP_LONG          2   // Длинный ответ (R2 для чтения CID/CSD)
#define SDIO_RESP_IGNORE_CRC    3   // Короткий ответ, где мы ОСОЗНАННО игнорируем ошибку CRC (R3 для ACMD41 и CMD55)

// Коды ошибок для возвращаемого значения
#define SD_CMD_OK               0
#define SD_CMD_TIMEOUT          1
#define SD_CMD_CRC_FAIL         2
#define SD_CMD_WAIT_ERR         3

#define SD_DATA_OK              0
#define SD_DATA_TIMEOUT         1
#define SD_DATA_CRC_FAIL        2
#define SD_DATA_FIFO_ERR        3

// Определения типов карт памяти
#define SD_CARD_UNKNOWN 0
#define SD_CARD_V1      1  // Старые карты SD Standard Capacity (<2ГБ)
#define SD_CARD_V2_SC   2  // Новые карты Standard Capacity
#define SD_CARD_V2_HC   3  // Карты High Capacity / Extended Capacity (от 4ГБ до 2ТБ)

void SDIO_Init(void);

uint8_t SD_Init_Card(void);
uint8_t SD_Get_Card_Address(void);
uint8_t SD_Select_Card(void);
uint8_t SD_Enable_4Bit_Bus(void);

void SDIO_Switch_To_High_Speed(void);
uint8_t SDIO_ReadBlock_Polling(uint32_t, uint32_t*);
uint8_t SDIO_ReadBlock_Interrupt(uint32_t, uint32_t*);

uint8_t SDIO_WriteBlock_Interrupt(uint32_t, uint32_t*);

uint8_t SDIO_SendCommand(uint8_t, uint32_t, uint8_t);
uint8_t SDIO_SendCommand_Polling(uint8_t, uint32_t, uint8_t);

void SDIO_DMA_Init(void);

void SDIO_DMA_Config_RX(uint32_t *buffer_out);

void SDIO_DMA_Config_TX(uint32_t *buffer_in);

uint8_t SDIO_ReadBlock_DMA(uint32_t block_addr, uint32_t *buffer_out);

uint8_t SDIO_WriteBlock_DMA(uint32_t block_addr, uint32_t *buffer_in);

#endif
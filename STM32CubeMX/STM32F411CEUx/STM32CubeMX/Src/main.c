//====================================================================================================
#include "main.h"
//====================================================================================================
void RCC_Configure(void);
void GPIO_Configure(void);
void SPI_Configure(void);
void USART_Configure(void);
//====================================================================================================
USART_InitTypeDef				USART_PC;
SPI_InitTypeDef 				SPI_W25Q;
SPI_InitTypeDef 				SPI_ST7735;

char USART_OutBuf[2048];
//====================================================================================================
void __FLASH_SPI_Select(bool select){
	if(select) GPIO_Pin_Low(GPIOA, GPIO_PIN_4);
	else GPIO_Pin_High(GPIOA, GPIO_PIN_4);
}

void __FLASH_SPI_Write(uint8_t byte){
	SPI_Transmit_Byte(SPI1, byte);
}

uint8_t __FLASH_SPI_Read(void){
	return SPI_Receive_Byte(SPI1);
}

W25Q_t MyFlash = {
	.W25Q_Select 	= __FLASH_SPI_Select,
	.W25Q_SPI_Write = __FLASH_SPI_Write,
	.W25Q_SPI_Read 	= __FLASH_SPI_Read
};
//====================================================================================================
extern volatile uint8_t  msc_write_request;
extern volatile uint32_t msc_write_lba;  

extern volatile uint8_t  msc_read_request;
extern volatile uint32_t msc_requested_lba;

extern volatile uint8_t  msc_scsi_cmd;

extern volatile uint32_t msc_remaining_bytes;
extern __ALIGN4 uint8_t msc_sector_buffer[1024];

__ALIGN4 static uint8_t flash_cache_buffer[4096]; 
volatile int32_t current_cached_sector_addr = -1; // -1 означает, что кэш пуст

void USB_MSC_Background_Process(void) {
	// Логика чтения
    if (msc_read_request) {
        SDIO->MASK = 0; 
        SDIO->ICR = 0xFFFFFFFF;

        // ВЫЗЫВАЕМ ЧТЕНИЕ ЧЕРЕЗ DMA!
        // Передаем адрес сектора и указатель на USB буфер
        uint8_t sd_status = SDIO_ReadBlock_DMA(msc_requested_lba, (uint32_t*)msc_sector_buffer);
        
        if (sd_status != 0) {
            // Ошибка чтения — глушим буфер нулями
            for (uint16_t i = 0; i < STORAGE_SECTOR_SIZE; i++) {
                msc_sector_buffer[i] = 0x00;
            }
        }

        msc_read_request = 0;

        uint32_t chunk = (msc_remaining_bytes > 64) ? 64 : msc_remaining_bytes;
        msc_remaining_bytes -= chunk;
        USB_EP_Tx(1, msc_sector_buffer, chunk); 
    }
	// Логика записи
    if (msc_write_request) {
        // Находим физический адрес начала 4 КБ сектора флешки (округляем вниз до 4096)
		volatile uint8_t write_status = SDIO_WriteBlock_DMA(msc_write_lba, (uint32_t*)msc_sector_buffer);

        // Сбрасываем флаг запроса на запись текущего сектора
        msc_write_request = 0;

        if (msc_remaining_bytes == 0) {
			delay_ms(50);
			// ПК закончил передачу. Принудительно сбрасываем накопленный кэш на физическую флешку
            //Flush_Flash_Cache();
            // Если все секторы от хоста приняты и записаны — закрываем команду
            msc_scsi_cmd = 0;
            MSC_Send_CSW(0);  // Отправляем хосту статус успешного завершения записи WRITE_10
            
            // Перевзводим точку на ожидание новой команды CBW
            EP1_OUT->DOEPTSIZ = (1U << USB_OTG_DOEPTSIZ_PKTCNT_Pos) | (64 << USB_OTG_DOEPTSIZ_XFRSIZ_Pos);
            EP1_OUT->DOEPCTL |= USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_CNAK;
        } 
        else {
			
            // Перевзводим точку OUT на прием следующего пакета данных секторов
            EP1_OUT->DOEPTSIZ = (1U << USB_OTG_DOEPTSIZ_PKTCNT_Pos) | (64 << USB_OTG_DOEPTSIZ_XFRSIZ_Pos);
            EP1_OUT->DOEPCTL |= USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_CNAK;
        }
    }
}
//====================================================================================================

void __ST7735_SPI_CS(bool state){
	if(state) GPIO_Pin_High(GPIOB, GPIO_PIN_14);
	else GPIO_Pin_Low(GPIOB, GPIO_PIN_14);
}

void __ST7735_SPI_DC(bool state){
	if(state) GPIO_Pin_High(GPIOA, GPIO_PIN_3);
	else GPIO_Pin_Low(GPIOA, GPIO_PIN_3);
}

void __ST7735_SPI_RST(bool state){
	if(state) GPIO_Pin_High(GPIOA, GPIO_PIN_2);
	else GPIO_Pin_Low(GPIOA, GPIO_PIN_2);
}

void __ST7735_SPI_Write(uint8_t byte){
	SPI_Transmit_Byte(SPI2, byte);
}

ST77xx_t MyDisplay = {
	.ST77xx_CS 			= __ST7735_SPI_CS,
	.ST77xx_DC 			= __ST7735_SPI_DC,
	.ST77xx_RST 		= __ST7735_SPI_RST,
	.ST77xx_SPI_Write 	= __ST7735_SPI_Write,
};
//====================================================================================================

FAT_Instance_t SD_FAT;

void SD_Read_For_FAT(uint8_t* buffer, uint32_t phys_address, uint32_t length) {
    // Вызываем вашу реальную функцию чтения низкого уровня
    SDIO_ReadBlock_DMA(phys_address, (uint32_t*)buffer);
}

void SD_Write_For_FAT(uint8_t* buffer, uint32_t phys_address, uint32_t length) {
	// Находим физический адрес начала 4 КБ сектора флешки (округляем вниз до 4096)
	uint32_t flash_sector_address = phys_address & 0xFFFFF000;
		
	// Вычисляем смещение (индекс) внутри 4 КБ кэша, куда запишутся новые 512 байт
	uint32_t cache_offset = phys_address % 4096;

	if (current_cached_sector_addr != -1 && current_cached_sector_addr != flash_sector_address) {
        //Flush_Flash_Cache(); 
    }

    // 4. Подгрузка кэша: если кэш пустой, считываем весь 4 КБ сектор с W25Q в ОЗУ
    if (current_cached_sector_addr == -1) {
        W25Q_FastRead(&MyFlash, flash_cache_buffer, flash_sector_address, 4096);
        current_cached_sector_addr = flash_sector_address;
    }

    // 5. Модификация данных в ОЗУ: копируем новые байты файловой системы поверх старых
    memcpy(&flash_cache_buffer[cache_offset], buffer, length);
}

typedef struct __attribute__((packed)) {
    /* --- Заголовок файла (BITMAPFILEHEADER — 14 байт) --- */
    uint16_t bfType;           // [0-1]   Сигнатура 'BM' (в Little-Endian это 0x4D42)
    uint32_t bfSize;           // [2-5]   Полный размер всего BMP-файла в байтах
    uint16_t bfReserved1;      // [6-7]   Зарезервировано (всегда 0)
    uint16_t bfReserved2;      // [8-9]   Зарезервировано (всегда 0)
    uint32_t bfOffBits;        // [10-13] Смещение в байтах от начала файла, где начинаются пиксели

    /* --- Заголовок изображения (BITMAPINFOHEADER — 40 байт) --- */
    uint32_t biSize;           // [14-17] Размер этого подзаголовка (всегда 40)
    uint32_t biWidth;          // [18-21] ШИРИНА ИЗОБРАЖЕНИЯ в пикселях (Width)
    uint32_t biHeight;         // [22-25] ВЫСОТА ИЗОБРАЖЕНИЯ в пикселях (Height)
    uint16_t biPlanes;         // [26-27] Количество плоскостей (всегда 1)
    uint16_t biBitCount;       // [28-29] ГЛУБИНА ЦВЕТА (у вашей картинки там будет 24 бита)
    uint32_t biCompression;    // [30-33] Тип сжатия (0 — без сжатия, BI_RGB)
    uint32_t biSizeImage;      // [34-37] Размер чистого массива пикселей в байтах
    uint32_t biXPelsPerMeter;  // [38-41] Горизонтальное разрешение (пикс/метр)
    uint32_t biYPelsPerMeter;  // [42-45] Вертикальное разрешение (пикс/метр)
    uint32_t biClrUsed;        // [46-49] Количество используемых цветов из палитры
    uint32_t biClrImportant;   // [50-53] Количество «важных» цветов (0 — все важные)
} BMP_Header_t;

BMP_Header_t bmp_info;
uint8_t row_buffer[300];

extern volatile uint16_t Card_RCA;          // Глобальная переменная для хранения адреса карты
extern volatile uint32_t Card_CID[4]; 

__attribute__((aligned(4))) uint32_t Sector_Buffer[128] = {0};
volatile uint8_t read_status = 0xFF;
volatile uint8_t write_status = 0xFF;

uint32_t Raw_Buffer[128]; // Сырой буфер ОЗУ для прерываний

extern uint8_t data_available;
extern uint8_t uart_cmd[4];

int main(void){
	RCC_Configure();
	GPIO_Configure();
	SPI_Configure();
	USART_Configure();
	
	SDIO_Init();

	
	
	char test_msg[128];

	USART_SendString(USART1, "\r\n");
	uint8_t sd_status = SD_Init_Card();

	sprintf(test_msg, "[SD Init] Init Status: 0x%02X\r\n", sd_status);
	USART_SendString(USART1, test_msg);

	if(sd_status == 0){
		sd_status = SD_Get_Card_Address();

		sprintf(test_msg, "[SD Init] Get Card Status: 0x%02X\r\n", sd_status);
		USART_SendString(USART1, test_msg);

		sprintf(test_msg, "[SD Init] RCA: 0x%04X\r\n", Card_RCA);
		USART_SendString(USART1, test_msg);

		sprintf(test_msg, "[SD Init] CID: 0x%04X%04X%04X%04X\r\n", Card_CID[0], Card_CID[1], Card_CID[2], Card_CID[3]);
		USART_SendString(USART1, test_msg);

		if(sd_status == 0){
			sd_status = SD_Select_Card();
			
			sprintf(test_msg, "[SD Init] Select Card Status: 0x%02X\r\n", sd_status);
			USART_SendString(USART1, test_msg);

			if(sd_status == 0){
				sd_status = SD_Enable_4Bit_Bus();

				sprintf(test_msg, "[SD Init] Enable 4 bit bus Status: 0x%02X\r\n", sd_status);
				USART_SendString(USART1, test_msg);

				if(sd_status == 0){
					SDIO_Switch_To_High_Speed();

					SD_FAT.disk_read = SD_Read_For_FAT;
					SD_FAT.disk_write = SD_Write_For_FAT;
						
					FAT_Mount(&SD_FAT);
					
				}
				else{
					sprintf(test_msg, "[SD Init] 4 bit bus disabled");
					USART_SendString(USART1, test_msg);
				}
			}
			else{
				sprintf(test_msg, "[SD Init] Card selection failed!");
				USART_SendString(USART1, test_msg);
			}
		}
		else{
			sprintf(test_msg, "[SD Init] Couldn't RCA nor CID addresses!");
			USART_SendString(USART1, test_msg);
		}
	}
	else{
		sprintf(test_msg, "[SD Init] Initialization failed!");
		USART_SendString(USART1, test_msg);
	}

	USB_Core_Init();
	
	while(1){
		USB_MSC_Background_Process();
		
		if(data_available){
			if(uart_cmd[0] == 0){
				read_status = SDIO_ReadBlock_DMA(0, Sector_Buffer);
						
				sprintf(test_msg, "[SD Read] Status: 0x%02X\r\n", read_status);
				USART_SendString(USART1, test_msg);

				uart_cmd[0] = 12;
			}
			if(uart_cmd[0] == 1){
				uint16_t sector_pointer = (uint16_t)(uart_cmd[1] << 8 | uart_cmd[2]);
				uint32_t cmd = (uint32_t)uart_cmd[3];
				Sector_Buffer[sector_pointer] = cmd; 
				write_status = SDIO_WriteBlock_DMA(0, Sector_Buffer);
				sprintf(test_msg, "[SD Write] Status: 0x%02X\r\n", write_status);
				USART_SendString(USART1, test_msg);

				sprintf(test_msg, "[SD Write] Pointer: 0x%04X Data: 0x%08X\r\n", sector_pointer, cmd);
				USART_SendString(USART1, test_msg);
				uart_cmd[0] = 12;
			}
			if(uart_cmd[0] == 2){
				USART_SendString(USART1, "\r\n--- SD DATA SECTOR 64 DUMP ---\r\n");
				for (uint8_t row = 0; row < 16; row++) {
								
								// 1. Выводим текущее HEX-смещение (адрес строки) для красоты
								sprintf(test_msg, "%04X: ", row * 32); // 16 строк по 32 байта
								USART_SendString(USART1, test_msg);

								// Внутренний цикл по 8 элементам uint32_t в текущей строке (8 * 4 = 32 байта)
								for (uint8_t col = 0; col < 8; col++) {
									
									// Рассчитываем правильный линейный индекс от 0 до 127
									uint32_t index = (row * 8) + col;

									// Форматируем ПОЛНОЕ 32-битное слово (8 hex-символов с ведущими нулями)
									// Добавляем пробел в конце для разделения колонок
									sprintf(test_msg, "%08X ", Sector_Buffer[index]);
									
									// Отправляем ВСЮ сформированную строку, а не только первый символ!
									USART_SendString(USART1, test_msg);
								}

								// В конце каждой строки делаем перенос каретки (\r\n)
								USART_SendString(USART1, "\r\n");
							}
				USART_SendString(USART1, "\r\n--- SD DATA SECTOR 64 DUMP ---\r\n");

				uart_cmd[0] = 12;
			}
			data_available = 0;
		}
	}

}


//====================================================================================================
void RCC_Configure(void){
	RCC_InitTypeDef 	RCC_InitStruct;
	
	RCC_InitStruct.ManualCalculatePLL			= false;
	
	RCC_InitStruct.OscillatorType				= OSC_EXTERNAL;	
	RCC_InitStruct.SystemClockSource			= PLL_CLK_SRC;
	
	RCC_InitStruct.ExternalOscillatorFreq 		= 25000000;
	
	RCC_InitStruct.AHB_Prescaler				= AHB_PRESCALER_DIV1;
	RCC_InitStruct.APB1_Prescaler				= APB_PRESCALER_DIV2;
	RCC_InitStruct.APB2_Prescaler				= APB_PRESCALER_DIV1;
		
	RCC_InitStruct.PLL_M_Divider				= 25;
	RCC_InitStruct.PLL_N_Multiplier				= 192;
	RCC_InitStruct.PLL_P_Divider				= PLL_P_DIV2;
	RCC_InitStruct.PLL_Q_Divider				= 4;
	
	SystemCoreClockConfigure(&RCC_InitStruct);
}

void GPIO_Configure(void){
	GPIO_InitTypeDef GPIO_InitStruct;

	// SDIO
	GPIO_InitStruct.Pin 		= GPIO_PIN_6 | GPIO_PIN_8 | GPIO_PIN_9;									
	GPIO_InitStruct.Mode 		= MODE_AF;
	GPIO_InitStruct.Type 		= TYPE_PP;
	GPIO_InitStruct.Speed 		= GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate	= AF_SDIO;
	GPIO_Init(GPIOA, &GPIO_InitStruct);

	GPIO_InitStruct.Pin 		= GPIO_PIN_4;									
	GPIO_InitStruct.Mode 		= MODE_AF;
	GPIO_InitStruct.Type 		= TYPE_PP;
	GPIO_InitStruct.Speed 		= GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate	= AF_SDIO;
	GPIO_Init(GPIOB, &GPIO_InitStruct);

	GPIO_InitStruct.Pin 		= GPIO_PIN_5;									
	GPIO_InitStruct.Mode 		= MODE_AF;
	GPIO_InitStruct.Type 		= TYPE_PP;
	GPIO_InitStruct.Speed 		= GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate	= AF_SDIO;
	GPIO_Init(GPIOB, &GPIO_InitStruct);

	GPIO_InitStruct.Pin 		= GPIO_PIN_15;									
	GPIO_InitStruct.Mode 		= MODE_AF;
	GPIO_InitStruct.Type 		= TYPE_PP;
	GPIO_InitStruct.Speed 		= GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate	= AF_SDIO;
	GPIO_Init(GPIOB, &GPIO_InitStruct);

	/*
	// SPI ST7735
	GPIO_InitStruct.Pin 		= GPIO_PIN_13 | GPIO_PIN_15;									
	GPIO_InitStruct.Mode 		= MODE_AF;
	GPIO_InitStruct.Type 		= TYPE_PP;
	GPIO_InitStruct.Speed 		= GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate	= AF_SPI2;
	GPIO_Init(GPIOB, &GPIO_InitStruct);
	

	// SPI ST7735 CS
	GPIO_InitStruct.Pin 			= GPIO_PIN_14;
	GPIO_InitStruct.Mode 			= MODE_OUTPUT;
	GPIO_InitStruct.Type 			= TYPE_PP;
	GPIO_InitStruct.Speed 			= GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_Init(GPIOB, &GPIO_InitStruct);

	// SPI ST7735 RST & DC
	GPIO_InitStruct.Pin 			= GPIO_PIN_2 | GPIO_PIN_3;
	GPIO_InitStruct.Mode 			= MODE_OUTPUT;
	GPIO_InitStruct.Type 			= TYPE_PP;
	GPIO_InitStruct.Speed 			= GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_Init(GPIOA, &GPIO_InitStruct);
	*/
	
	// UART
	GPIO_InitStruct.Pin 			= GPIO_PIN_6 | GPIO_PIN_7;
	GPIO_InitStruct.Mode 			= MODE_AF;
	GPIO_InitStruct.Type 			= TYPE_PP;
	GPIO_InitStruct.Speed 		= GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate	= AF_USART1;
	GPIO_Init(GPIOB, &GPIO_InitStruct);

	// USB (PA11(DM), PA12(DP))
	
	GPIO_InitStruct.Pin 			= GPIO_PIN_11 | GPIO_PIN_12;	
	GPIO_InitStruct.Mode 			= MODE_AF;
	GPIO_InitStruct.Type 			= TYPE_PP;
	GPIO_InitStruct.Speed 		= GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.Alternate	= AF_USB_OTG_FS;
	GPIO_Init(GPIOA, &GPIO_InitStruct);
	
	
}

void SPI_Configure(void){
	SPI_ST7735.Baudrate_Prescaler = SPI_BAUDRATE_DIV16;
	SPI_Init(SPI2, &SPI_ST7735);
}

void USART_Configure(void){
	USART_PC.Baudrate = 115200;
	
	USART_PC.Rx_Enable = true;
	USART_PC.Tx_Enable = true;
	
	USART_PC.Rx_IRq_Enable = true;
	
	USART_Init(USART1, &USART_PC);
}
//====================================================================================================


#include "SDIO.h"

volatile uint32_t SD_Command_Status = 0;
uint32_t SD_Card_Buffer[4] = {0}; // Буфер для хранения длинных ответов (R2)

volatile uint32_t *pSDIO_Read_Buffer = 0;
volatile uint32_t SDIO_Words_To_Read = 0;
volatile uint8_t  SDIO_Transfer_Status = 0xFF; // 0 - успех, 1 - ошибка/таймаут

volatile uint32_t *pSDIO_Write_Buffer = 0;
volatile uint32_t SDIO_Words_To_Write = 0;

void SDIO_Clock_Config(void){
    // Включаем тактирование шины SDIO
    RCC->APB2ENR |= RCC_APB2ENR_SDIOEN;

    // Делаем принудительный аппаратный сброс блока SDIO
    RCC->APB2RSTR |= RCC_APB2RSTR_SDIORST;
    for(volatile int i = 0; i < 10; i++); // Короткая пауза
    RCC->APB2RSTR &= ~RCC_APB2RSTR_SDIORST;
}

void SDIO_Adapter_Enable(void){
    // Очищаем регистр конфигурации тактов SDIO
    SDIO->CLKCR = 0;

    // Выставляем делитель CLKDIV для получения от 100 до 400 кГц (48 МГц / (DIV + 2))
    SDIO->CLKCR |= (238 << SDIO_CLKCR_CLKDIV_Pos);

    // Выключаем режим энергосберегающего отключения клока (PWRSAV = 0)
    SDIO->CLKCR &= ~SDIO_CLKCR_PWRSAV;

    // По умолчанию выставляем 1-битный режим (WIDBUS = 00b)
    // Карта физически не поймет 4-битную шину, пока мы не пройдем инициализацию
    SDIO->CLKCR &= ~SDIO_CLKCR_WIDBUS;

    // Включаем генерацию тактового сигнала на ножку CLK (Бит CLKEN = 1)
    SDIO->CLKCR |= SDIO_CLKCR_CLKEN;

    // Подаем питание на сам SDIO (Power State = 11b)
    SDIO->POWER = SDIO_POWER_PWRCTRL; 
    
    // Даем время стабилизироваться цепям питания карты памяти
    for(volatile int i = 0; i < 50000; i++);
}

void SDIO_NVIC_Enable(void){
    SDIO->MASK = 0;
    
    // Очищаем все возможные старые флаги статуса
    SDIO->ICR = 0xFFFFFFFF;

    SDIO->MASK |= SDIO_MASK_CMDRENDIE | SDIO_MASK_CTIMEOUTIE | SDIO_MASK_CCRCFAILIE | SDIO_MASK_CMDSENTIE;

    NVIC_SetPriority(SDIO_IRQn, 3); 

    // Включаем глобальное прерывание SDIO в контроллере NVIC
    NVIC_EnableIRQ(SDIO_IRQn);
}

void SDIO_PowerOn_Cycles(void) {
    // Очищаем все старые флаги, аргументы и автоматы
    SDIO->ICR = 0xFFFFFFFF;
    SDIO->ARG = 0x00000000;
    SDIO->CMD = 0;   // СТРОГО ОБНУЛЯЕМ! Автомат команд должен спать!
    SDIO->DCTRL = 0; // Автомат данных должен спать!
    
    // Питание и CLKEN уже включены в Adapter_Enable,
    // тактовый сигнал идет на ножку CLK (без бита CPSMEN).
    // Крутим пустой цикл, давая карте получить её обязательные 74+ такта
    // для принудительного сброса High-Speed режима.
    for (volatile int i = 0; i < 25000; i++) {
        __NOP(); 
    }
    
    // 3. Зачищаем мусорные флаги, которые могли хаотично взвестись от дребезга
    SDIO->ICR = 0xFFFFFFFF;
}

uint8_t Card_Type = SD_CARD_UNKNOWN;

uint32_t SD_Response_Data[4] = {0, 0, 0, 0};

void SDIO_Init(void){
    SDIO_Clock_Config();
    SDIO_Adapter_Enable();
    SDIO_PowerOn_Cycles();
    
    for (volatile int i = 0; i < 150000; i++); 
}

uint8_t SDIO_SendCommand_Polling(uint8_t cmd_index, uint32_t argument, uint8_t resp_type) {
    volatile uint32_t timeout_cnt; 
    
    // Сброс всех старых флагов
    SDIO->ICR = 0xFFFFFFFF;

    // Ждем окончания прошлой активности
    timeout_cnt = 0x000FFFFF; 
    while ((SDIO->STA & SDIO_STA_CMDACT) && timeout_cnt) {
        timeout_cnt--;
    }
    if (timeout_cnt == 0) return SD_CMD_TIMEOUT;

    // Аргумент
    SDIO->ARG = argument;

    // Конфигурация регистра управления командой (SDIO->CMD)
    uint32_t cmd_reg = (cmd_index & SDIO_CMD_CMDINDEX);

    if (resp_type == SDIO_RESP_SHORT || resp_type == SDIO_RESP_IGNORE_CRC) {
        cmd_reg |= SDIO_CMD_WAITRESP_0;  // Короткий ответ (01b)
    } 
    else if (resp_type == SDIO_RESP_LONG) {
        cmd_reg |= (SDIO_CMD_WAITRESP_0 | SDIO_CMD_WAITRESP_1); // Длинный ответ (11b)
    }

    // Включаем автомат команд (CPSMEN) — старт передачи в шину
    cmd_reg |= SDIO_CMD_CPSMEN;
    SDIO->CMD = cmd_reg;

    // 5. Цикл ожидания флагов завершения (Строго для команд!)
    if (resp_type == SDIO_RESP_NONE) {
        timeout_cnt = 0x000FFFFF; 
        while (!(SDIO->STA & (SDIO_STA_CMDSENT | SDIO_STA_CTIMEOUT)) && timeout_cnt) {
            timeout_cnt--;
        }
    } 
    else {
        timeout_cnt = 0x000FFFFF; 
        while (!(SDIO->STA & (SDIO_STA_CMDREND | SDIO_STA_CTIMEOUT | SDIO_STA_CCRCFAIL)) && timeout_cnt) {
            timeout_cnt--;
        }
    }

    uint32_t status = SDIO->STA;

    if (timeout_cnt == 0 || (status & SDIO_STA_CTIMEOUT)) {
        SDIO->ICR = SDIO_STA_CTIMEOUT; 
        return SD_CMD_TIMEOUT;         
    }

    if ((status & SDIO_STA_CCRCFAIL) && (resp_type != SDIO_RESP_IGNORE_CRC)) {
        SDIO->ICR = SDIO_STA_CCRCFAIL; 
        return SD_CMD_CRC_FAIL;    
    }

    // Вычитывание ответа
    if (resp_type == SDIO_RESP_LONG) {
        SD_Response_Data[0] = SDIO->RESP1;
        SD_Response_Data[1] = SDIO->RESP2;
        SD_Response_Data[2] = SDIO->RESP3;
        SD_Response_Data[3] = SDIO->RESP4;
    } 
    else if (resp_type == SDIO_RESP_SHORT || resp_type == SDIO_RESP_IGNORE_CRC) {
        SD_Response_Data[0] = SDIO->RESP1;
    }

    // Очищаем финишные флаги команд
    SDIO->ICR = SDIO_STA_CMDREND | SDIO_STA_CMDSENT | SDIO_STA_CTIMEOUT | SDIO_STA_CCRCFAIL;


    return SD_CMD_OK; 
}

uint8_t SD_Get_Speed_Class_DMA(SD_Card_t* SD_Struct) {
    uint8_t status;
    uint32_t rca_arg = ((uint32_t)SD_Struct->Card_RCA << 16);

    status = SDIO_SendCommand_Polling(55, rca_arg, SDIO_RESP_SHORT);
    if (status != SD_CMD_OK) return 0x99;

    uint32_t buffer_out[16] = {0};
    
    SDIO->MASK = 0;   
    
    // Гасим автомат данных и триггер DMAEN.
    // Даем шине AHB время разорвать старую связь со Стримом 6
    SDIO->DCTRL = 0; 
    for(volatile int i = 0; i < 500; i++) { __NOP(); } // Пауза обязательна!
    
    SDIO->ICR = 0xFFFFFFFF;    
    while (SDIO->STA & (SDIO_STA_TXACT | SDIO_STA_RXACT));

    // Отправляем команду чтения ACMD13
    status = SDIO_SendCommand_Polling(13, 0x00000000, SDIO_RESP_SHORT);
    if (status != SD_CMD_OK) return 1;

    // Настраиваем параметры длины и таймаута блока данных
    SDIO->DLEN = 64;          
    SDIO->DTIMER = 0x00FFFFFF; 
    SDIO->ICR = 0xFFFFFFFF; 

    // Предварительно настраиваем автомат данных SDIO на прием
    SDIO->DCTRL = (6 << SDIO_DCTRL_DBLOCKSIZE_Pos) | SDIO_DCTRL_DTDIR | SDIO_DCTRL_DMAEN;

    // Включаем аппаратную перекачку
    SDIO_DMA_Config_RX(buffer_out);

    // Даем старт автомату данных SDIO
    SDIO->DCTRL |= SDIO_DCTRL_DTEN;

    // Ждем физического окончания блока
    while (!(SDIO->STA & (SDIO_STA_DBCKEND | SDIO_STA_DTIMEOUT | SDIO_STA_DCRCFAIL | SDIO_STA_RXOVERR)));

    uint32_t sta_reg = SDIO->STA;
    
    SDIO->DCTRL = 0; 
    for(volatile int i = 0; i < 500; i++) { __NOP(); }
    
    DMA2->LIFCR = 0x0FC00000; // Очищаем Стрим 3
    SDIO->ICR = 0xFFFFFFFF;

    uint8_t raw_class = (uint8_t)(buffer_out[2] & 0xFF);

    // Превращаем сырой байт спецификации в понятную цифру класса (2, 4, 6, 10)
    if (raw_class == 0x00)      SD_Struct->Class = 0;  // Класс не определен
    else if (raw_class == 0x01) SD_Struct->Class = 2;  // Class 2
    else if (raw_class == 0x02) SD_Struct->Class = 4;  // Class 4
    else if (raw_class == 0x03) SD_Struct->Class = 6;  // Class 6
    else if (raw_class == 0x04) SD_Struct->Class = 10; // Class 10

    return 0; 

}

uint8_t SD_Init_Card(SD_Card_t* SD_Struct) {
    uint8_t status;
    uint32_t acmd41_arg = 0;

    SD_Struct->CardType = SD_CARD_UNKNOWN;

    // CMD0 (Сброс). Ответ не нужен.
    status = SDIO_SendCommand_Polling(0, 0x00000000, SDIO_RESP_NONE);
    if (status != SD_CMD_OK) return 0x01;

    for (volatile int i = 0; i < 150000; i++); // Пауза

    // CMD8. Проверка паттерна 0xAA ОБЯЗАТЕЛЬНО идет с валидацией CRC (SDIO_RESP_SHORT)
    status = SDIO_SendCommand_Polling(8, 0x000001AA, SDIO_RESP_SHORT);
    if (status == SD_CMD_OK) {
        // Карта ответила -> это V2! Проверяем эхо-паттерн.
        if ((SD_Response_Data[0] & 0xFF) != 0xAA) return 0x02; // Сбой паттерна
        
        SD_Struct->CardType = SD_CARD_V2_SC; // Временно считаем V2_SC, в цикле уточним до HC
        acmd41_arg = 0x40FF8000;             // Выставляем бит HCS (Бит 30) поддержка высокой емкости
    } else {
        // Карта НЕ ответила, это старая карта V1 (или шина совсем мертва)
        SD_Struct->CardType = SD_CARD_V1;
        acmd41_arg = 0x00FF8000;             // Бит HCS равен 0, старые карты его не поймут
    }

    volatile uint32_t retry = 500;
    while (retry > 0) {
        status = SDIO_SendCommand_Polling(55, 0x00000000, SDIO_RESP_IGNORE_CRC);
        if (status == SD_CMD_OK) {
            status = SDIO_SendCommand_Polling(41, acmd41_arg, SDIO_RESP_IGNORE_CRC);
            if (status == SD_CMD_OK) {
                // Если карта ответила (Busy бит 31 равен 1) она проснулась
                if (SD_Response_Data[0] & (1UL << 31)) {
                    if (SD_Response_Data[0] & (1UL << 30)) {
                        SD_Struct->CardType = SD_CARD_V2_HC;
                    }
                    
                    break; 
                }
            }
        }
        for (volatile int delay = 0; delay < 25000; delay++);
        retry--;
    }

    if (retry == 0) return 0x04;

    status = SD_Get_Card_Address(SD_Struct);

    if(status == SD_CMD_OK){
        status = SD_Get_Card_CSD(SD_Struct);

        if(status == SD_CMD_OK){
            status = SD_Select_Card(SD_Struct);

            if(status == SD_CMD_OK){
                status = SD_Enable_4Bit_Bus(SD_Struct);

                

                if(status == SD_CMD_OK){
                    SDIO_Switch_To_High_Speed();

                    status = SD_Get_Speed_Class_DMA(SD_Struct);

                    if(status == SD_CMD_OK){
                        return SD_CMD_OK; // УСПЕХ!
                    }
                    else{
                        return 0x09; 
                    }

                    
                }
                else{ return 0x08; }
            }
            else{ return 0x07; }
        }
        else{ return 0x06; }
    }
    else{ return 0x05; }

    
}

uint8_t SD_Get_Card_Address(SD_Card_t* SD_Struct) {
    uint8_t status;

    // Отправляем CMD2 (Запрос CID). Аргумент = 0. Ответ = LONG (R2)
    // Без этой команды карта не позволит запросить адрес.
    status = SDIO_SendCommand_Polling(2, 0x00000000, SDIO_RESP_LONG);
    if (status != SD_CMD_OK) {
        return 0x09; // Ошибка: Карта не отдала CID регистр
    }

    // Сохраняем CID в глобальный буфер
    SD_Struct->Card_CID[0] = SD_Response_Data[0];
    SD_Struct->Card_CID[1] = SD_Response_Data[1];
    SD_Struct->Card_CID[2] = SD_Response_Data[2];
    SD_Struct->Card_CID[3] = SD_Response_Data[3];

    SD_Struct->CardVersion = (SD_Struct->Card_CID[1] >> 24) & 0xFF;

    // Отправляем CMD3 (Запрос RCA адреса). Аргумент = 0. Ответ = SHORT (R6)
    status = SDIO_SendCommand_Polling(3, 0x00000000, SDIO_RESP_SHORT);
    if (status != SD_CMD_OK) {
        return 0x0A; // Ошибка: Карта не прислала свой адрес RCA
    }

    // По спецификации SD, в ответе R6 на команду CMD3 карта присылает 
    // свой сгенерированный RCA адрес в СТАРШИХ 16 битах регистра RESP1 (биты 16-31).
    SD_Struct->Card_RCA = (SD_Response_Data[0] >> 16) & 0xFFFF;

    // Проверяем, что адрес не равен 0
    if (SD_Struct->Card_RCA == 0) {
        return 0x0B;
    }

    return SD_CMD_OK;
}

volatile uint32_t Card_CSD[4] = {0};

uint8_t SD_Get_Card_CSD(SD_Card_t* SD_Struct) {
    uint8_t status;

    // Проверяем, что RCA адрес уже был получен
    if (SD_Struct->Card_RCA == 0) {
        return 0x0C; // RCA адрес карты неизвестен, нельзя вызвать CMD9
    }

    // Отправляем CMD9 (Запрос CSD). 
    // Аргумент = RCA адрес в старших 16 битах (биты 16-31). 
    // Ответ = LONG (R2), содержащий 128 бит данных регистра CSD.
    uint32_t argument = (uint32_t)SD_Struct->Card_RCA << 16;
    
    status = SDIO_SendCommand_Polling(9, argument, SDIO_RESP_LONG);
    if (status != SD_CMD_OK) {
        return 0x0D; // Карта не отдала CSD регистр
    }

    Card_CSD[0] = SD_Response_Data[0];
    Card_CSD[1] = SD_Response_Data[1];
    Card_CSD[2] = SD_Response_Data[2];
    Card_CSD[3] = SD_Response_Data[3];

    uint8_t csd[16];
    csd[0]  = (uint8_t)(Card_CSD[0] >> 24);
    csd[1]  = (uint8_t)(Card_CSD[0] >> 16);
    csd[2]  = (uint8_t)(Card_CSD[0] >> 8);
    csd[3]  = (uint8_t)(Card_CSD[0]);
    csd[4]  = (uint8_t)(Card_CSD[1] >> 24);
    csd[5]  = (uint8_t)(Card_CSD[1] >> 16);
    csd[6]  = (uint8_t)(Card_CSD[1] >> 8);
    csd[7]  = (uint8_t)(Card_CSD[1]);
    csd[8]  = (uint8_t)(Card_CSD[2] >> 24);
    csd[9]  = (uint8_t)(Card_CSD[2] >> 16);
    csd[10] = (uint8_t)(Card_CSD[2] >> 8);
    csd[11] = (uint8_t)(Card_CSD[2]);
    csd[12] = (uint8_t)(Card_CSD[3] >> 24);
    csd[13] = (uint8_t)(Card_CSD[3] >> 16);
    csd[14] = (uint8_t)(Card_CSD[3] >> 8);
    csd[15] = (uint8_t)(Card_CSD[3]);

    if (SD_Struct->CardType == SD_CARD_V2_HC) {
        // ДЛЯ КАРТ ВЫСОКОЙ ЕМКОСТИ (SDHC/SDXC)
        
        //  Физический размер блока для карт высокой емкости жестко равен 512 байт
        SD_Struct->BlockSize = 512;

        // Вычисляем 22-битное поле Device Size (C_SIZE) по байтам
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16) | 
                          ((uint32_t)csd[8] << 8) | 
                          ((uint32_t)csd[9]);
        
        // Официальная формула из спецификации SD v2.0: (C_SIZE + 1) * 1024
        // Для ваших данных (c_size = 30000) это даст ровно 30 721 024 блоков (14.65 ГБ чистой емкости)
        SD_Struct->BlockNbr = (c_size + 1) * 1024;
    } 
    else {
        // ДЛЯ СТАРЫХ КАРТ STANDARD CAPACITY (V1 / V2_SC до 2 ГБ)
        
        // Поле Max. Read Data Block Length (READ_BL_LEN) в младших 4 битах csd[5]
        uint32_t read_bl_len = csd[5] & 0x0F;
        
        // Физический размер блока равен 2^READ_BL_LEN
        SD_Struct->BlockSize = 1UL << read_bl_len;

        // 12-битное поле C_SIZE для старых карт из байт csd[6], csd[7] и csd[8]
        uint32_t c_size = ((uint32_t)(csd[6] & 0x03) << 10) | 
                          ((uint32_t)csd[7] << 2) | 
                          ((uint32_t)(csd[8] >> 6) & 0x03);
                          
        // Извлекаем 3-битное поле C_SIZE_MULT из битов csd[9] и csd[10]
        uint32_t c_size_mult = ((uint32_t)(csd[9] & 0x03) << 1) | 
                               ((uint32_t)(csd[10] >> 7) & 0x01);

        // Поле 5: Вычисляем количество блоков по формуле спецификации v1.0
        uint32_t mult = 1UL << (c_size_mult + 2);
        SD_Struct->BlockNbr = (c_size + 1) * mult;

        // Коррекция: если физический блок равен 1024 байта, пересчитываем 
        // объем в стандартные 512-байтовые блоки для совместимости с FAT-драйверами
        if (SD_Struct->BlockSize == 1024) {
            SD_Struct->BlockNbr *= 2;
            SD_Struct->BlockSize = 512;
        }
    }

    SD_Struct->CardSpeed = csd[3];

    return SD_CMD_OK;
}



uint8_t SD_Select_Card(SD_Card_t* SD_Struct) {
    uint8_t status;

    // Аргумент: Card_RCA в старших 16 битах, младшие 16 бит равны 0
    uint32_t argument = ((uint32_t)SD_Struct->Card_RCA << 16);

    status = SDIO_SendCommand_Polling(7, argument, SDIO_RESP_SHORT);
    if (status != SD_CMD_OK) {
        return 0x88; // Карта не выбралась
    }

    return SD_CMD_OK;
}

uint8_t SD_Enable_4Bit_Bus(SD_Card_t* SD_Struct) {
    uint8_t status;
    uint32_t rca_arg = ((uint32_t)SD_Struct->Card_RCA << 16);

    // Сообщаем карте памяти о переходе (CMD55 + ACMD6)
    status = SDIO_SendCommand_Polling(55, rca_arg, SDIO_RESP_SHORT);
    if (status != SD_CMD_OK) return 0x99;

    status = SDIO_SendCommand_Polling(6, 0x00000002, SDIO_RESP_SHORT);
    if (status != SD_CMD_OK) return 0xAA;

    // SDIO_NVIC_Enable();

    SDIO->CLKCR &= ~SDIO_CLKCR_WIDBUS;  // Сбрасываем старую разрядность
    SDIO->CLKCR |= SDIO_CLKCR_WIDBUS_0; // Выставляем 4-битный режим (01b)

    return SD_CMD_OK;
}

void SDIO_Switch_To_High_Speed(void) {
    SDIO->CLKCR &= ~SDIO_CLKCR_CLKEN;
    // Сбрасываем старый делитель частоты
    SDIO->CLKCR &= ~SDIO_CLKCR_CLKDIV;

    // Выставляем новый делитель. 
    // Для 16 МГц: прописываем 1 (0x01)
    // Для 24 МГц: прописываем 0 (0x00)
    SDIO->CLKCR |= (22 << SDIO_CLKCR_CLKDIV_Pos); 

    // SDIO->CLKCR |= SDIO_CLKCR_NEGEDGE;

    // Добавляем бит аппаратного контроля потока данных (HWFCEN), чтобы линии D0-D3 не рассинхронизировались
    SDIO->CLKCR |= SDIO_CLKCR_HWFC_EN;

    SDIO->CLKCR |= SDIO_CLKCR_CLKEN;

    // Даем генератору окончательно стабилизироваться на новой частоте
    for (volatile int i = 0; i < 950000; i++);

    
}

void SDIO_DMA_Init(void) {
    // Включаем тактирование контроллера DMA2 в регистре AHB1ENR
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    for(volatile int i = 0; i < 50; i++);
}

void SDIO_DMA_Config_RX(uint32_t *buffer_out) {
    // Включаем тактирование контроллера DMA2
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    __DSB();
    
    // Выключаем стрим для изменения настроек
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    while(DMA2_Stream3->CR & DMA_SxCR_EN); // Ждем гарантированного выключения
    
    // Очищаем старые флаги ошибок Стрима 3 в регистре LIFCR (маска 0x0FC00000)
    DMA2->LIFCR = 0x0FC00000; 

    // Обнуляем регистр конфигурации перед записью новых настроек
    DMA2_Stream3->CR = 0;
    while(DMA2_Stream3->CR != 0);

    // Задаем базовые адреса
    DMA2_Stream3->PAR = (uint32_t)&(SDIO->FIFO);    // Источник: FIFO SDIO
    DMA2_Stream3->M0AR = (uint32_t)buffer_out;      // Приемник: массив в ОЗУ
    DMA2_Stream3->NDTR = 128;                       // 128 слов (512 байт)

    // Конфигурируем регистр управления CR:
    // CHSEL = 4, PL = 2, MSIZE = 2, PSIZE = 2, MINC = 1, DIR = 00b (Периферия -> Память)
    // PBURST = 01b (4 слова), MBURST = 01b (4 слова)
    DMA2_Stream3->CR = (4 << DMA_SxCR_CHSEL_Pos)  | (2 << DMA_SxCR_PL_Pos)   |
                       (2 << DMA_SxCR_MSIZE_Pos)  | (2 << DMA_SxCR_PSIZE_Pos)  |
                       DMA_SxCR_MINC              |
                       (1 << DMA_SxCR_PBURST_Pos) | (1 << DMA_SxCR_MBURST_Pos) |
                       DMA_SxCR_PFCTRL;

    // Включаем FIFO режим контроллера DMA (FCR)
    // FTH = 11b (Полный буфер FIFO DMA)
    DMA2_Stream3->FCR = DMA_SxFCR_DMDIS | (3 << DMA_SxFCR_FTH_Pos);

    // Очищаем флаги повторно прямо перед запуском
    DMA2->LIFCR = 0x0FC00000; 

    // 5. Физически активируем стрим DMA на прием
    DMA2_Stream3->CR |= DMA_SxCR_EN;
    while(!(DMA2_Stream3->CR & DMA_SxCR_EN));
}

void SDIO_DMA_Config_TX(uint32_t *buffer_in) {
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    __DSB();

    DMA2_Stream6->CR &= ~DMA_SxCR_EN;
    while(DMA2_Stream6->CR & DMA_SxCR_EN);
    
    DMA2->HIFCR = 0x003F0000; 
    DMA2_Stream6->CR = 0; 
    while(DMA2_Stream6->CR != 0); 

    DMA2_Stream6->PAR = (uint32_t)&(SDIO->FIFO);
    DMA2_Stream6->M0AR = (uint32_t)buffer_in;      

    DMA2_Stream6->NDTR = 128; 

    DMA2_Stream6->CR = (4 << DMA_SxCR_CHSEL_Pos)   | (2 << DMA_SxCR_PL_Pos)    |
                       (2 << DMA_SxCR_MSIZE_Pos)   | (2 << DMA_SxCR_PSIZE_Pos)   |
                       DMA_SxCR_MINC               | (1 << DMA_SxCR_DIR_Pos)     |
                       (1 << DMA_SxCR_PBURST_Pos)  | (1 << DMA_SxCR_MBURST_Pos) |
                       DMA_SxCR_PFCTRL;

    DMA2_Stream6->FCR = DMA_SxFCR_DMDIS | (3 << DMA_SxFCR_FTH_Pos); 

    DMA2->HIFCR = 0x003F0000; 

    DMA2_Stream6->CR |= DMA_SxCR_EN;
    while(!(DMA2_Stream6->CR & DMA_SxCR_EN)); 
}

uint8_t SDIO_ReadBlock_DMA(SD_Card_t* SD_Struct, uint32_t block_addr, uint32_t *buffer_out) {
    uint8_t status;
    
    SDIO->MASK = 0;   
    
    SDIO->DCTRL = 0; 
    for(volatile int i = 0; i < 500; i++) { __NOP(); } 
    
    SDIO->ICR = 0xFFFFFFFF;    
    while (SDIO->STA & (SDIO_STA_TXACT | SDIO_STA_RXACT));

    status = SDIO_SendCommand_Polling(17, block_addr, SDIO_RESP_SHORT);
    if (status != SD_CMD_OK) return 1;

    SDIO->DLEN = 512;          
    SDIO->DTIMER = 0x00FFFFFF; 
    SDIO->ICR = 0xFFFFFFFF; 

    SDIO->DCTRL = (9 << SDIO_DCTRL_DBLOCKSIZE_Pos) | SDIO_DCTRL_DTDIR | SDIO_DCTRL_DMAEN;

    SDIO_DMA_Config_RX(buffer_out);

    SDIO->DCTRL |= SDIO_DCTRL_DTEN;

    while (!(SDIO->STA & (SDIO_STA_DBCKEND | SDIO_STA_DTIMEOUT | SDIO_STA_DCRCFAIL | SDIO_STA_RXOVERR)));

    uint32_t sta_reg = SDIO->STA;
    
    SDIO->DCTRL = 0; 
    for(volatile int i = 0; i < 500; i++) { __NOP(); }
    
    DMA2->LIFCR = 0x0FC00000; 
    SDIO->ICR = 0xFFFFFFFF;

    return 0; 
}



uint8_t SDIO_WriteBlock_DMA(SD_Card_t* SD_Struct, uint32_t block_addr, uint32_t *buffer_in) {
    uint8_t status;

    __attribute__((aligned(4))) static volatile uint32_t local_aligned_buffer[128];
    
    uint8_t *src_bytes = (uint8_t*)buffer_in;
    uint8_t *dst_bytes = (uint8_t*)local_aligned_buffer;
    for(int i = 0; i < 512; i++) {
        dst_bytes[i] = src_bytes[i];
    }

    __DSB();
    __ISB();

    SDIO->MASK = 0;   
    
    SDIO->DCTRL = 0; 
    for(volatile int i = 0; i < 500; i++) { __NOP(); }
    
    SDIO->ICR = 0xFFFFFFFF;    
    while (SDIO->STA & (SDIO_STA_TXACT | SDIO_STA_RXACT));

    status = SDIO_SendCommand_Polling(24, block_addr, SDIO_RESP_SHORT);
    if (status != SD_CMD_OK) {
        DMA2_Stream6->CR &= ~DMA_SxCR_EN;
        SDIO->DCTRL = 0;
        SDIO->ICR = 0xFFFFFFFF;
        return 1;
    }

    SDIO_DMA_Config_TX((uint32_t*)local_aligned_buffer);

    SDIO->DLEN = 512;          
    SDIO->DTIMER = 0x00FFFFFF; 
    SDIO->ICR = 0xFFFFFFFF;

    SDIO->DCTRL = (9 << SDIO_DCTRL_DBLOCKSIZE_Pos) | SDIO_DCTRL_DMAEN | SDIO_DCTRL_DTEN;

    volatile uint32_t timeout_gate = 0x00FFFFFF;
    while (!(SDIO->STA & (SDIO_STA_DATAEND | SDIO_STA_DTIMEOUT | SDIO_STA_DCRCFAIL | SDIO_STA_TXUNDERR)) && timeout_gate) {
        timeout_gate--;
    }
    
    if(timeout_gate == 0){
        return 0x01;
    }

    DMA2_Stream6->CR &= ~DMA_SxCR_EN;
    
    volatile uint32_t dma_disable_timeout = 50000;
    while((DMA2_Stream6->CR & DMA_SxCR_EN) && --dma_disable_timeout) {
        __NOP();
    }

    SDIO->DCTRL = 0;
    for(volatile int i = 0; i < 500; i++) { __NOP(); }

    timeout_gate = 30000; 
    while (timeout_gate--) {
        status = SDIO_SendCommand_Polling(13, ((uint32_t)SD_Struct->Card_RCA << 16), SDIO_RESP_SHORT);
        
        if (status == SD_CMD_OK) {
            uint32_t response = SD_Response_Data[0];
            uint32_t card_state = (response >> 9) & 0x0F;
            
            if (response & (1U << 8)) {
                if (card_state == 3 || card_state == 4) {
                    break; 
                }
            }
        }
        
        for(volatile int delay = 0; delay < 100; delay++) { __NOP(); }
    }

    if (timeout_gate == 0) {
        DMA2_Stream6->CR &= ~DMA_SxCR_EN;
        SDIO->DCTRL = 0;
        SDIO->ICR = 0xFFFFFFFF;
        return 0xAA;
    }

    DMA2->HIFCR = 0xFFFFFFFF;
    DMA2->LIFCR = 0xFFFFFFFF;
    SDIO->ICR = 0xFFFFFFFF;

    return 0; 
}





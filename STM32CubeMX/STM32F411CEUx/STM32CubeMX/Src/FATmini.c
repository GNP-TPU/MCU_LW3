#include "FATmini.h"

#define tolower(c)  (((c) >= 'A' && (c) <= 'Z') ? ((c) + 32) : (c))

static uint8_t __attribute__((aligned(4))) lba_buffer[512];

static const uint8_t lfn_offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

uint8_t FAT_Mount(FATmini_t* FAT_Struct) {
    if (FAT_Struct == 0 || FAT_Struct->DiskRead == 0) {
        return 1; // Ошибка: диск не привязан к аппаратному чтению
    }

    FAT_Struct->DiskRead(0, lba_buffer);


    FAT_Struct->Sign    =   (uint16_t)lba_buffer[510] |
                            (uint16_t)lba_buffer[511] << 8;

    if (FAT_Struct->Sign != 0xAA55) {
        return 3; // Не валидная разметка диска
    }

    for(uint8_t i = 0; i < 3; i++){
        FAT_Struct->JumpBoot[i] =  (uint8_t)lba_buffer[0 + i]; 
    }

    for(uint8_t i = 0; i < 8; i++){
        FAT_Struct->OEMName[i] =  (uint8_t)lba_buffer[3 + i]; 
    }
    
    FAT_Struct->BytesPerSector          =   (uint16_t)lba_buffer[11] |
                                            (uint16_t)lba_buffer[12] << 8;

    FAT_Struct->SectorsPerCluster       =   (uint8_t)lba_buffer[13];

    FAT_Struct->ReservedSectorsCount    =   (uint16_t)lba_buffer[14] |
                                            (uint16_t)lba_buffer[15] << 8;

    FAT_Struct->NumFATs                 =   (uint8_t)lba_buffer[16];      
    
    FAT_Struct->RootEntriesCount        =   (uint16_t)lba_buffer[17] |
                                            (uint16_t)lba_buffer[18] << 8;

    FAT_Struct->MediaDescriptor         =   (uint8_t)lba_buffer[21];   

    FAT_Struct->FAT.FAT12_16.TotalSectors16     =   (uint16_t)lba_buffer[19] |
                                                    (uint16_t)lba_buffer[20] << 8; // 0 for FAT32

    FAT_Struct->FAT_StartSector     = FAT_Struct->ReservedSectorsCount;
    FAT_Struct->FAT_StartAddress    = FAT_Struct->FAT_StartSector * FAT_Struct->BytesPerSector;

    

    if(FAT_Struct->FAT.FAT12_16.TotalSectors16 != 0){
        FAT_Struct->FAT_Type = FAT_TYPE_12_16;

        FAT_Struct->FAT.FAT12_16.TotalSectors16     =   (uint16_t)lba_buffer[19] |
                                                        (uint16_t)lba_buffer[20] << 8; // 0 for FAT32
        
        FAT_Struct->FAT.FAT12_16.FAT_Size_16        =   (uint16_t)lba_buffer[22] |
                                                        (uint16_t)lba_buffer[23] << 8; // 0 for FAT32
        
        FAT_Struct->TotalSectors                = (uint32_t)FAT_Struct->FAT.FAT12_16.TotalSectors16;

        FAT_Struct->FAT_NumSectors              = FAT_Struct->FAT.FAT12_16.FAT_Size_16 * FAT_Struct->NumFATs;


        FAT_Struct->RootDirectory_StartSector   = FAT_Struct->FAT_StartSector + FAT_Struct->FAT_NumSectors;
        
        

        FAT_Struct->Cluster2_StartSector =  FAT_Struct->RootDirectory_StartSector + 
                                            (FAT_Struct->RootEntriesCount * 32 + FAT_Struct->BytesPerSector - 1) /
                                            FAT_Struct->BytesPerSector;

        FAT_Struct->CountOfClusters =   (FAT_Struct->TotalSectors - FAT_Struct->ReservedSectorsCount - 
                                        FAT_Struct->FAT.FAT12_16.FAT_Size_16 * FAT_Struct->NumFATs -
                                        (FAT_Struct->RootEntriesCount * 32 + FAT_Struct->BytesPerSector - 1) /
                                        FAT_Struct->BytesPerSector) / FAT_Struct->SectorsPerCluster;

        FAT_Struct->RootDirectory_NumSectors    = FAT_Struct->Cluster2_StartSector - FAT_Struct->RootDirectory_StartSector;                                        


    }
    else{
        FAT_Struct->FAT_Type = FAT_TYPE_32;

        FAT_Struct->FAT.FAT32.TotalSectors32    =   (uint32_t)lba_buffer[32]        |
                                                    (uint32_t)lba_buffer[33] << 8   |
                                                    (uint32_t)lba_buffer[34] << 16  |
                                                    (uint32_t)lba_buffer[35] << 24; 
        
        FAT_Struct->FAT.FAT32.FAT_Size_32       =   (uint32_t)lba_buffer[36]        |
                                                    (uint32_t)lba_buffer[37] << 8   |
                                                    (uint32_t)lba_buffer[38] << 16  |
                                                    (uint32_t)lba_buffer[39] << 24;
                                                
        FAT_Struct->FAT.FAT32.RootCluster       =   (uint32_t)lba_buffer[44]        |
                                                    (uint32_t)lba_buffer[45] << 8   |
                                                    (uint32_t)lba_buffer[46] << 16  |
                                                    (uint32_t)lba_buffer[47] << 24;

        FAT_Struct->TotalSectors            = (uint32_t)FAT_Struct->FAT.FAT32.TotalSectors32;

        FAT_Struct->FAT_NumSectors          = FAT_Struct->FAT.FAT32.FAT_Size_32 * FAT_Struct->NumFATs;

        

        FAT_Struct->Cluster2_StartSector    = FAT_Struct->FAT_StartSector + FAT_Struct->FAT_NumSectors;

        FAT_Struct->RootDirectory_StartSector    =   FAT_Struct->Cluster2_StartSector +
                                                    ((FAT_Struct->FAT.FAT32.RootCluster - 2) *
                                                    FAT_Struct->SectorsPerCluster);

        FAT_Struct->CountOfClusters =   (FAT_Struct->TotalSectors - FAT_Struct->ReservedSectorsCount - 
                                        FAT_Struct->FAT.FAT32.FAT_Size_32 * FAT_Struct->NumFATs) / 
                                        FAT_Struct->SectorsPerCluster;

        FAT_Struct->RootDirectory_NumSectors    = 0;                                        

    }

    if(FAT_Struct->CountOfClusters <= 4085){
        FAT_Struct->FAT_Type = FAT_TYPE_12;
        FAT_Struct->FAT_Type_String = "FAT12";
    }
    else if(FAT_Struct->CountOfClusters >= 4086 && FAT_Struct->CountOfClusters <= 65525){
        FAT_Struct->FAT_Type = FAT_TYPE_16;
        FAT_Struct->FAT_Type_String = "FAT16";
    }
    else if(FAT_Struct->CountOfClusters >= 65526){
        FAT_Struct->FAT_Type = FAT_TYPE_32;
        FAT_Struct->FAT_Type_String = "FAT32";
    }
    else{
        FAT_Struct->FAT_Type = FAT_TYPE_UNKNOWN;
        FAT_Struct->FAT_Type_String = "FAT UNKNOWN";
    }

    return 0; // Успешно скопировано в структуру!
}

uint32_t FAT_GetNextCluster(FATmini_t* FAT_Struct, uint32_t Current_Cluster) {
    
    // === ВЕТКА 1: Чтение для FAT12 ===
    if (FAT_Struct->FAT_Type == FAT_TYPE_12) {
        // 1. Вычисляем абсолютное смещение в байтах от начала таблицы FAT
        uint32_t FAT_Offset_Byte = (Current_Cluster * 3) / 2;
        
        // 2. Вычисляем относительные номера секторов (0, 1, 2...) внутри FAT
        uint32_t FAT_Sector1 = FAT_Offset_Byte / 512;
        uint32_t FAT_Sector2 = (FAT_Offset_Byte + 1) / 512;
        
        // Смещение первого байта внутри сектора
        uint32_t Local_Offset_Byte = FAT_Offset_Byte % 512;
        
        // Абсолютные номера секторов на диске (LBA)
        // Предполагается, что fat1_addr хранит номер стартового сектора FAT
        uint32_t Sector1 = FAT_Struct->FAT_StartSector + FAT_Sector1;
        uint32_t Sector2 = FAT_Struct->FAT_StartSector + FAT_Sector2;

        uint8_t byte0 = 0;
        uint8_t byte1 = 0;

        // 3. Читаем первый сектор (только если его еще нет в кэше)
        FAT_Struct->DiskRead(Sector1, lba_buffer);
        byte0 = lba_buffer[Local_Offset_Byte];

        // 4. Проверяем стык секторов
        if (Sector1 == Sector2) {
            // Оба байта в одном секторе — берем второй байт из этого же буфера
            byte1 = lba_buffer[Local_Offset_Byte + 1];
        } 
        else {
            FAT_Struct->DiskRead(Sector2, lba_buffer);
            byte1 = lba_buffer[0]; // Первый байт нового сектора
        }

        // 5. Собираем 16-битное значение
        uint16_t raw_entry = (uint16_t)byte0 | ((uint16_t)byte1 << 8);
        
        // 6. Выделяем 12 бит в зависимости от четности кластера
        if (Current_Cluster % 2 == 0) {
            return raw_entry & 0x0FFF; // Четный кластер — младшие 12 бит
        } else {
            return raw_entry >> 4;     // Нечетный кластер — старшие 12 бит
        }
    }

    if (FAT_Struct->FAT_Type == FAT_TYPE_16) {
        // Каждая запись занимает ровно 2 байта. Стыков секторов быть не может, 
        // так как 512 байт делится на 2 без остатка (в секторе ровно 256 записей).
        uint32_t FAT_Offset_Byte = Current_Cluster * 2;
        uint32_t Target_Sector = FAT_Struct->FAT_StartSector + (FAT_Offset_Byte / 512);
        uint32_t Local_Offset = FAT_Offset_Byte % 512;

        
        FAT_Struct->DiskRead(Target_Sector, lba_buffer);

        // Читаем 16-битное значение побайтово во избежание Alignment Fault
        uint16_t next_cluster = (uint16_t)lba_buffer[Local_Offset] | 
                               ((uint16_t)lba_buffer[Local_Offset + 1] << 8);
        return next_cluster;
    }

    else if (FAT_Struct->FAT_Type == FAT_TYPE_32) {
        // Каждая запись занимает ровно 4 байта. Стыков секторов также нет,
        // так как 512 делится на 4 без остатка (в секторе ровно 128 записей).
        uint32_t FAT_Offset_Byte = Current_Cluster * 4;
        uint32_t Target_Sector = FAT_Struct->FAT_StartSector + (FAT_Offset_Byte / 512);
        uint32_t Local_Offset = FAT_Offset_Byte % 512;

        FAT_Struct->DiskRead(Target_Sector, lba_buffer);

        // Собираем 32-битное значение побайтово
        uint32_t next_cluster = (uint32_t)lba_buffer[Local_Offset]       |
                               ((uint32_t)lba_buffer[Local_Offset + 1] << 8)  |
                               ((uint32_t)lba_buffer[Local_Offset + 2] << 16) |
                               ((uint32_t)lba_buffer[Local_Offset + 3] << 24);
        
        // В FAT32 старшие 4 бита зарезервированы, их необходимо маскировать
        return next_cluster & 0x0FFFFFFF;
    }
    
    return 0x0FFFFFFF; // Если тип неизвестен, возвращаем универсальный 32-битный конец файла
}

bool FAT_FindFile(FATmini_t* FAT_Struct, char* FileName) {
    char compiled_lfn[256] = {0};
    bool lfn_is_valid = false;
    
    // Статический массив смещений для побайтового чтения символов LFN
    static const uint8_t lfn_offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

    if(FAT_Struct->FAT_Type == FAT_TYPE_32){
        // --- УНИВЕРСАЛЬНЫЙ ПОСЛЕДОВАТЕЛЬНЫЙ ОБХОД ДЛЯ FAT32 ---

        for (uint32_t CurrentCluster = FAT_Struct->Directory.CurrentCluster; 
             CurrentCluster < 0x0FFFFFF8 && CurrentCluster >= 2; 
             CurrentCluster = FAT_GetNextCluster(FAT_Struct, CurrentCluster)) {
            
            // Читаем все сектора внутри текущего кластера
            for (uint8_t sector = 0; sector < FAT_Struct->SectorsPerCluster; sector++) {
                uint32_t CurrentSector = FAT_Struct->Cluster2_StartSector + 
                                         ((CurrentCluster - 2) * FAT_Struct->SectorsPerCluster) + sector;
                FAT_Struct->DiskRead(CurrentSector, lba_buffer);

                for (uint8_t block = 0; block < 16; block++) {
                    uint8_t* CurrentBlock = &lba_buffer[block * 32];

                    // --- НАЧАЛО ПАРСИНГА БЛОКА ---
                    if (CurrentBlock[0] == 0x00){ return false; } // Конец каталога
                    if (CurrentBlock[0] == 0xE5) { lfn_is_valid = false; continue; } // Удален

                    uint8_t attr = CurrentBlock[11];

                    // ИСПРАВЛЕНО: Сначала строго проверяем на LFN (0x0F)
                    if (attr == 0x0F) {
                        uint8_t seq = CurrentBlock[0];
                        if (seq & 0x40) {
                            seq &= ~0x40;
                            if (seq <= 20) {
                                lfn_is_valid = true;
                                memset(compiled_lfn, 0, sizeof(compiled_lfn));
                            }
                        }
                        if (lfn_is_valid && seq >= 1) {
                            int char_offset = (seq - 1) * 13;
                            for (uint8_t k = 0; k < 13; k++) {
                                int total_offset = char_offset + k;

                                if (total_offset >= 255) {
                                    break;
                                }

                                uint8_t low_byte  = CurrentBlock[lfn_offsets[k]];
                                uint8_t high_byte = CurrentBlock[lfn_offsets[k] + 1];

                                if ((low_byte == 0x00 && high_byte == 0x00) || 
                                    (low_byte == 0xFF && high_byte == 0xFF)) {
                                    break;
                                }

                                if (high_byte == 0x00) {
                                    compiled_lfn[total_offset] = (char)low_byte;
                                } else {
                                    compiled_lfn[total_offset] = '?';
                                }
                            }
                        }
                        continue; // Уходим на следующий блок, накапливая LFN
                    }

                    // ИСПРАВЛЕНО: Только если это НЕ LFN, проверяем маску метки тома (Volume ID)
                    if (attr & 0x08) { lfn_is_valid = false; continue; } 

                    // --- ПРОВЕРКА СОВПАДЕНИЯ ИМЕНИ ---
                    bool is_match = false;

                    // 1. Сначала проверяем совпадение по собранному LFN имени
                    if (lfn_is_valid && compiled_lfn[0] != '\0') {
                        int idx = 0;
                        while (FileName[idx] && compiled_lfn[idx] && 
                               (tolower((unsigned char)FileName[idx]) == tolower((unsigned char)compiled_lfn[idx]))) {
                            idx++;
                        }
                        if (FileName[idx] == '\0' && compiled_lfn[idx] == '\0') {
                            is_match = true;
                        }
                    }

                    // 2. Если по LFN не совпало, собираем и проверяем SFN (короткое имя)
                    if (!is_match) {
                        char sfn_name[13];
                        int p = 0;

                        for (int i = 0; i < 8; i++) {
                            if (CurrentBlock[i] != ' ') {
                                sfn_name[p++] = (char)CurrentBlock[i];
                            }
                        }

                        if (CurrentBlock[8] != ' ') {
                            sfn_name[p++] = '.';
                            for (int i = 8; i < 11; i++) {
                                if (CurrentBlock[i] != ' ') {
                                    sfn_name[p++] = (char)CurrentBlock[i];
                                }
                            }
                        }
                        sfn_name[p] = '\0';

                        int idx = 0;
                        while (FileName[idx] && sfn_name[idx] && 
                               (tolower((unsigned char)FileName[idx]) == tolower((unsigned char)sfn_name[idx]))) {
                            idx++;
                        }
                        if (FileName[idx] == '\0' && sfn_name[idx] == '\0') {
                            is_match = true;
                        }
                    }

                    // --- ИЗВЛЕЧЕНИЕ МЕТАДАННЫХ ПРИ СОВПАДЕНИИ ---
                    if (is_match) {
                        FAT_Struct->Directory.Attr = attr; 
                        
                        uint32_t cluster_hi = ((uint32_t)CurrentBlock[21] << 8) | CurrentBlock[20];
                        uint32_t cluster_lo = ((uint32_t)CurrentBlock[27] << 8) | CurrentBlock[26];
                        
                        FAT_Struct->Directory.FirstCluster = (cluster_hi << 16) | cluster_lo;

                        uint32_t file_size = ((uint32_t)CurrentBlock[31] << 24) |
                                             ((uint32_t)CurrentBlock[30] << 16) |
                                             ((uint32_t)CurrentBlock[29] << 8)  |
                                             CurrentBlock[28];

                        FAT_Struct->Directory.FileSize = file_size;

                        return true; 
                    }

                    lfn_is_valid = false;
                    // --- КОНЕЦ ПАРСИНГА БЛОКА ---
                }
            }
        }
    }

    else {
        // --- ОБХОД ДЛЯ FAT12 / FAT16 ---
        
        // 1. Проверяем по порядку: если стартовый кластер равен 0, значит мы ищем в КОРНЕ
        if (FAT_Struct->Directory.CurrentCluster == 0) {
            
            for (uint32_t sector = 0; sector < FAT_Struct->RootDirectory_NumSectors; sector++) {
                uint32_t CurrentSector = FAT_Struct->RootDirectory_StartSector + sector;
                FAT_Struct->DiskRead(CurrentSector, lba_buffer);

                // Перебираем все 16 блоков по 32 байта в секторе
                for (uint8_t block = 0; block < 16; block++) {
                    uint8_t* CurrentBlock = &lba_buffer[block * 32];
                    
                    // --- НАЧАЛО ПАРСИНГА БЛОКА ---
                    if (CurrentBlock[0] == 0x00){ return false; } // Конец каталога
                    if (CurrentBlock[0] == 0xE5) { lfn_is_valid = false; continue; } // Удален

                    uint8_t attr = CurrentBlock[11];

                    // ИСПРАВЛЕНО: Сначала строго проверяем на LFN (0x0F)
                    if (attr == 0x0F) {
                        uint8_t seq = CurrentBlock[0];
                        if (seq & 0x40) {
                            seq &= ~0x40;
                            if (seq <= 20) {
                                lfn_is_valid = true;
                                memset(compiled_lfn, 0, sizeof(compiled_lfn));
                            }
                        }
                        if (lfn_is_valid && seq >= 1) {
                            int char_offset = (seq - 1) * 13;
                            for (uint8_t k = 0; k < 13; k++) {
                                int total_offset = char_offset + k;

                                if (total_offset >= 255) {
                                    break;
                                }

                                uint8_t low_byte  = CurrentBlock[lfn_offsets[k]];
                                uint8_t high_byte = CurrentBlock[lfn_offsets[k] + 1];

                                if ((low_byte == 0x00 && high_byte == 0x00) || 
                                    (low_byte == 0xFF && high_byte == 0xFF)) {
                                    break;
                                }

                                if (high_byte == 0x00) {
                                    compiled_lfn[total_offset] = (char)low_byte;
                                } else {
                                    compiled_lfn[total_offset] = '?';
                                }
                            }
                        }
                        continue; // Уходим на накопление LFN-имени
                    }

                    // ИСПРАВЛЕНО: Только если это не LFN, отсекаем метку тома
                    if (attr & 0x08) { lfn_is_valid = false; continue; } 

                    // --- ПРОВЕРКА СОВПАДЕНИЯ ИМЕНИ ---
                    bool is_match = false;

                    if (lfn_is_valid && compiled_lfn[0] != '\0') {
                        int idx = 0;
                        while (FileName[idx] && compiled_lfn[idx] && 
                            (tolower((unsigned char)FileName[idx]) == tolower((unsigned char)compiled_lfn[idx]))) {
                            idx++;
                        }
                        if (FileName[idx] == '\0' && compiled_lfn[idx] == '\0') {
                            is_match = true;
                        }
                    }

                    if (!is_match) {
                        char sfn_name[13];
                        int p = 0;

                        for (int i = 0; i < 8; i++) {
                            if (CurrentBlock[i] != ' ') {
                                sfn_name[p++] = (char)CurrentBlock[i];
                            }
                        }

                        if (CurrentBlock[8] != ' ') {
                            sfn_name[p++] = '.';
                            for (int i = 8; i < 11; i++) {
                                if (CurrentBlock[i] != ' ') {
                                    sfn_name[p++] = (char)CurrentBlock[i];
                                }
                            }
                        }
                        sfn_name[p] = '\0';

                        int idx = 0;
                        while (FileName[idx] && sfn_name[idx] && 
                            (tolower((unsigned char)FileName[idx]) == tolower((unsigned char)sfn_name[idx]))) {
                            idx++;
                        }
                        if (FileName[idx] == '\0' && sfn_name[idx] == '\0') {
                            is_match = true;
                        }
                    }

                    // --- ИЗВЛЕЧЕНИЕ МЕТАДАННЫХ ПРИ СОВПАДЕНИИ ---
                    if (is_match) {
                        FAT_Struct->Directory.Attr = attr; 
                        uint32_t cluster_hi = ((uint32_t)CurrentBlock[21] << 8) | CurrentBlock[20];
                        uint32_t cluster_lo = ((uint32_t)CurrentBlock[27] << 8) | CurrentBlock[26];

                        FAT_Struct->Directory.FirstCluster = (cluster_hi << 16) | cluster_lo;

                        uint32_t file_size  =   ((uint32_t)CurrentBlock[31] << 24) |
                                                ((uint32_t)CurrentBlock[30] << 16) |
                                                ((uint32_t)CurrentBlock[29] << 8)  |
                                                CurrentBlock[28];

                        FAT_Struct->Directory.FileSize = file_size;

                        return true; 
                    }

                    lfn_is_valid = false;
                    // --- КОНЕЦ ПАРСИНГА БЛОКА ---
                }
            }
        }
        else {
            uint32_t eoc_marker = (FAT_Struct->FAT_Type == FAT_TYPE_16) ? 0xFFF8 : 0x0FF8;
            // 2. Сценарий Б: Мы зашли в ПОДПАПКУ в FAT12/16 (она обходится по кластерам)
            for (uint32_t CurrentCluster = FAT_Struct->Directory.CurrentCluster; 
                 CurrentCluster < eoc_marker && CurrentCluster >= 2; 
                 CurrentCluster = FAT_GetNextCluster(FAT_Struct, CurrentCluster)) {
                
                for (uint8_t sector = 0; sector < FAT_Struct->SectorsPerCluster; sector++) {
                    uint32_t CurrentSector = FAT_Struct->Cluster2_StartSector + 
                                             ((CurrentCluster - 2) * FAT_Struct->SectorsPerCluster) + sector;
                    FAT_Struct->DiskRead(CurrentSector, lba_buffer);

                    for (uint8_t block = 0; block < 16; block++) {
                        uint8_t* CurrentBlock = &lba_buffer[block * 32];

                        // --- ПАРСИНГ БЛОКА ПОДПАПКИ FAT12/16 (абсолютно идентичен верхнему) ---
                        if (CurrentBlock[0] == 0x00){ return false; }
                        if (CurrentBlock[0] == 0xE5) { lfn_is_valid = false; continue; }

                        uint8_t attr = CurrentBlock[11];

                        if (attr == 0x0F) {
                            uint8_t seq = CurrentBlock[0];
                            if (seq & 0x40) {
                                seq &= ~0x40;
                                if (seq <= 20) {
                                    lfn_is_valid = true;
                                    memset(compiled_lfn, 0, sizeof(compiled_lfn));
                                }
                            }
                            if (lfn_is_valid && seq >= 1) {
                                int char_offset = (seq - 1) * 13;
                                for (uint8_t k = 0; k < 13; k++) {
                                    int total_offset = char_offset + k;
                                    if (total_offset >= 255) break;

                                    uint8_t low_byte  = CurrentBlock[lfn_offsets[k]];
                                    uint8_t high_byte = CurrentBlock[lfn_offsets[k] + 1];

                                    if ((low_byte == 0x00 && high_byte == 0x00) || (low_byte == 0xFF && high_byte == 0xFF)) break;

                                    if (high_byte == 0x00) {
                                        compiled_lfn[total_offset] = (char)low_byte;
                                    } else {
                                        compiled_lfn[total_offset] = '?';
                                    }
                                }
                            }
                            continue;
                        }

                        if (attr & 0x08) { lfn_is_valid = false; continue; } 

                        bool is_match = false;
                        if (lfn_is_valid && compiled_lfn[0] != '\0') {
                            int idx = 0;
                            while (FileName[idx] && compiled_lfn[idx] && (tolower((unsigned char)FileName[idx]) == tolower((unsigned char)compiled_lfn[idx]))) idx++;
                            if (FileName[idx] == '\0' && compiled_lfn[idx] == '\0') is_match = true;
                        }

                        if (!is_match) {
                            char sfn_name[13];
                            int p = 0;
                            for (int i = 0; i < 8; i++) if (CurrentBlock[i] != ' ') sfn_name[p++] = (char)CurrentBlock[i];
                            if (CurrentBlock[8] != ' ') {
                                sfn_name[p++] = '.';
                                for (int i = 8; i < 11; i++) if (CurrentBlock[i] != ' ') sfn_name[p++] = (char)CurrentBlock[i];
                            }
                            sfn_name[p] = '\0';

                            int idx = 0;
                            while (FileName[idx] && sfn_name[idx] && (tolower((unsigned char)FileName[idx]) == tolower((unsigned char)sfn_name[idx]))) idx++;
                            if (FileName[idx] == '\0' && sfn_name[idx] == '\0') is_match = true;
                        }

                        if (is_match) {
                            FAT_Struct->Directory.Attr = attr; 
                            
                            uint32_t cluster_hi = ((uint32_t)CurrentBlock[21] << 8) | CurrentBlock[20];
                            uint32_t cluster_lo = ((uint32_t)CurrentBlock[27] << 8) | CurrentBlock[26];
                            
                            FAT_Struct->Directory.FirstCluster = (cluster_hi << 16) | cluster_lo;
                            
                            uint32_t file_size  =   ((uint32_t)CurrentBlock[31] << 24) |
                                                    ((uint32_t)CurrentBlock[30] << 16) |
                                                    ((uint32_t)CurrentBlock[29] << 8)  |
                                                    CurrentBlock[28];
                            
                            FAT_Struct->Directory.FileSize = file_size;
                            
                            return true;
                        }
                        
                        lfn_is_valid = false;
                    }
                }
            }
        }
    }

    return false; // Файл не найден
}

bool FAT_OpenFile(FATmini_t* FAT_Struct, char* FileName) {
    // 1. Вызываем поиск файла. 
    // В FAT_Struct->Directory.CurrentCluster в этот момент уже должен лежать кластер текущей папки!
    bool FoundFile = FAT_FindFile(FAT_Struct, FileName);

    // 2. Если элемент найден, проверяем его метаданные
    if (FoundFile == true) {
        
        // ЗАЩИТА: Если нашли папку вместо файла — выходим с ошибкой
        if (FAT_Struct->Directory.Attr & 0x10) {
            return false; 
        }

        // Устанавливаем указатель текущей побайтовой позиции в 0 (начало файла)
        FAT_Struct->Directory.CurrentPosition = 0;
        
        // Переводим переменную CurrentCluster из режима "кластер папки, где искали"
        // в режим "кластер файла, который мы сейчас будем читать"
        FAT_Struct->Directory.CurrentCluster = FAT_Struct->Directory.FirstCluster;
    }

    return FoundFile; 
}

bool FAT_OpenDirectory(FATmini_t* FAT_Struct, char* DirName) {
    // 1. Ищем папку внутри текущего каталога
    bool Found = FAT_FindFile(FAT_Struct, DirName);

    // 2. Если нашли, проверяем, что это точно папка
    if (Found == true) {
        if (FAT_Struct->Directory.Attr & 0x10) {
            
            // ИСПРАВЛЕНО: Переключаем компас поиска на кластер этой новой папки.
            // Теперь следующий вызов FAT_FindFile начнет искать файлы именно внутри неё!
            FAT_Struct->Directory.CurrentCluster = FAT_Struct->Directory.FirstCluster;
            
            return true; // Успешно вошли в папку
        }
    }
    return false; // Не нашли или это оказался файл
}

void FAT_ReturnRootDirectory(FATmini_t* FAT_Struct) {
    // Проверяем по порядку тип файловой системы на соответствие вашему enum из FAT_Mount
    if (FAT_Struct->FAT_Type == FAT_TYPE_32) {
        // Для FAT32 возвращаем указатели на базовый корневой кластер
        FAT_Struct->Directory.FirstCluster   = FAT_Struct->FAT.FAT32.RootCluster;
        FAT_Struct->Directory.CurrentCluster = FAT_Struct->FAT.FAT32.RootCluster;
    } 
    else {
        // Для FAT12 и FAT16 корнем на диске является маркер 0
        FAT_Struct->Directory.FirstCluster   = 0;
        FAT_Struct->Directory.CurrentCluster = 0;
    }

    // Сбрасываем позицию и размер, так как в корне мы ищем новые элементы с нуля
    FAT_Struct->Directory.CurrentPosition = 0;
    FAT_Struct->Directory.FileSize        = 0;
    
    // Выставляем атрибут директории (папки), так как корень — это папка
    FAT_Struct->Directory.Attr            = 0x10; 
}

bool FAT_ReadFile(FATmini_t* FAT_Struct, uint8_t* OutBuffer, uint32_t StartByte, uint32_t Length) {
    // Проверка входных данных: если структуры нет, буфер пустой или длина 0 — выходим
    if (FAT_Struct == 0 || OutBuffer == 0 || Length == 0) {
        return false;
    }

    // Определяем, что перед нами: файл или папка (проверяем бит директории 0x10)
    bool IsDirectory = (FAT_Struct->Directory.Attr & 0x10) ? true : false;

    // 1. ОГРАНИЧЕНИЕ РАЗМЕРА (Применяется строго для файлов)
    if (IsDirectory == false) {
        // Если точка старта уже за пределами файла — читать нечего
        if (StartByte >= FAT_Struct->Directory.FileSize) {
            return false;
        }
        // Если просят прочесть больше, чем осталось до конца файла, урезаем длину до реального остатка
        if (StartByte + Length > FAT_Struct->Directory.FileSize) {
            Length = FAT_Struct->Directory.FileSize - StartByte;
        }
    }

    // Вычисляем размер одного кластера в байтах
    uint32_t BytesPerCluster = FAT_Struct->SectorsPerCluster * FAT_Struct->BytesPerSector;

    // 2. НАВИГАЦИЯ ДО СТАРТОВОЙ ТОЧКИ (Промотка цепочки кластеров до StartByte)
    // Начинаем отсчет с самого первого кластера файла/папки
    uint32_t CurrentCluster = FAT_Struct->Directory.FirstCluster;
    uint32_t ClustersToSkip = StartByte / BytesPerCluster; // Сколько кластеров нужно пропустить

    // Смещение внутри целевого кластера, где начнется чтение
    uint32_t ByteOffsetInCluster = StartByte % BytesPerCluster;

    // Шагаем по таблице FAT до нужного кластера, где расположен StartByte
    for (uint32_t i = 0; i < ClustersToSkip; i++) {
        CurrentCluster = FAT_GetNextCluster(FAT_Struct, CurrentCluster);
        
        // Определяем маску конца цепочки (EOC) в зависимости от типа файловой системы
        uint32_t EocMarker = (FAT_Struct->FAT_Type == FAT_TYPE_32) ? 0x0FFFFFF8 : 
                             (FAT_Struct->FAT_Type == FAT_TYPE_16) ? 0xFFF8 : 0x0FF8;

        if (CurrentCluster >= EocMarker || CurrentCluster < 2) {
            return false; // Ошибка: StartByte указывает за пределы выделенной цепочки
        }
    }

    // 3. ПОСЛЕДОВАТЕЛЬНЫЙ ЦИКЛ ЧТЕНИЯ ДАННЫХ
    uint32_t BytesRead = 0;

    // Работаем в цикле, пока полностью не заберем Length байт
    while (BytesRead < Length) {
        
        // Вычисляем индекс сектора внутри кластера и смещение байта внутри этого сектора
        uint32_t SectorInCluster = ByteOffsetInCluster / FAT_Struct->BytesPerSector;
        uint32_t ByteOffsetInSector = ByteOffsetInCluster % FAT_Struct->BytesPerSector;

        // Рассчитываем физический номер сектора на диске
        uint32_t TargetSector = FAT_Struct->Cluster2_StartSector + 
                                ((CurrentCluster - 2) * FAT_Struct->SectorsPerCluster) + 
                                SectorInCluster;

        // Читаем сектор с диска в ваш глобальный буфер lba_buffer
        FAT_Struct->DiskRead(TargetSector, lba_buffer);

        // Определяем, сколько байт доступно для копирования из текущего сектора
        uint32_t BytesAvailableInSector = FAT_Struct->BytesPerSector - ByteOffsetInSector;
        uint32_t BytesToCopy = Length - BytesRead;

        if (BytesToCopy > BytesAvailableInSector) {
            BytesToCopy = BytesAvailableInSector;
        }

        // Побайтово копируем данные из глобального lba_buffer напрямую в ваш OutBuffer
        for (uint32_t i = 0; i < BytesToCopy; i++) {
            OutBuffer[BytesRead + i] = lba_buffer[ByteOffsetInSector + i];
        }

        // Обновляем счетчики прочитанного объема
        BytesRead += BytesToCopy;
        ByteOffsetInCluster += BytesToCopy;

        // 4. ПЕРЕХОД НА СЛЕДУЮЩИЙ КЛАСТЕР
        // Если позиция сравнялась с размером кластера — берем следующий из таблицы FAT
        if (ByteOffsetInCluster >= BytesPerCluster) {
            CurrentCluster = FAT_GetNextCluster(FAT_Struct, CurrentCluster);
            ByteOffsetInCluster = 0; // В новом кластере чтение пойдет с 0-го байта

            uint32_t EocMarker = (FAT_Struct->FAT_Type == FAT_TYPE_32) ? 0x0FFFFFF8 : 
                                 (FAT_Struct->FAT_Type == FAT_TYPE_16) ? 0xFFF8 : 0x0FF8;

            // Если цепочка кластеров прервалась
            if (CurrentCluster >= EocMarker || CurrentCluster < 2) {
                // Для файла это ошибка (размер не совпал с цепочкой), для папки — нормальный финал сырых данных
                if (IsDirectory == false) {
                    return false; 
                } else {
                    break; 
                }
            }
        }
    }

    // Фиксируем финальные маркеры положения в структуре
    FAT_Struct->Directory.CurrentPosition = StartByte + BytesRead;
    FAT_Struct->Directory.CurrentCluster  = CurrentCluster;

    return true; // Чтение завершено успешно!
}
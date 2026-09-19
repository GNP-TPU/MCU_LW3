#include "FATmini.h"

static const uint8_t lfn_offsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

uint8_t FAT_Mount(FAT_Instance_t* instance) {
    if (instance == 0 || instance->disk_read == 0) {
        return 1; // Ошибка: диск не привязан к аппаратному чтению
    }

    static uint8_t __attribute__((aligned(4))) lba_buffer[512];
    // 1. Читаем сектор 0 с W25Q в наш выровненный буфер
    instance->disk_read(lba_buffer, 0, 512);

    // 2. Проверяем сигнатуру 55 AA в конце сектoра
    if (lba_buffer[510] != 0x55 || lba_buffer[511] != 0xAA) {
        return 3; // Не валидная разметка диска
    }

	// Заполняем базовую геометрию (ваша часть, расширенная дальше)
	instance->bpb.bytes_per_sector    = (uint16_t)lba_buffer[11] | ((uint16_t)lba_buffer[12] << 8);
	instance->bpb.sectors_per_cluster = lba_buffer[13];
	instance->bpb.reserved_sectors    = (uint16_t)lba_buffer[14] | ((uint16_t)lba_buffer[15] << 8);
	instance->bpb.num_fats            = lba_buffer[16];
	instance->bpb.root_entry_count    = (uint16_t)lba_buffer[17] | ((uint16_t)lba_buffer[18] << 8);
	instance->bpb.total_sectors_16    = (uint16_t)lba_buffer[19] | ((uint16_t)lba_buffer[20] << 8);
	instance->bpb.media_descriptor    = lba_buffer[21];
	instance->bpb.sectors_per_fat_16  = (uint16_t)lba_buffer[22] | ((uint16_t)lba_buffer[23] << 8);
	instance->bpb.sectors_per_track   = (uint16_t)lba_buffer[24] | ((uint16_t)lba_buffer[25] << 8);
	instance->bpb.num_heads           = (uint16_t)lba_buffer[26] | ((uint16_t)lba_buffer[27] << 8);

	// 32-битные поля собираем из 4 байт подряд
	instance->bpb.hidden_sectors      = (uint32_t)lba_buffer[28] | 
							((uint32_t)lba_buffer[29] << 8) | 
							((uint32_t)lba_buffer[30] << 16) | 
							((uint32_t)lba_buffer[31] << 24);

	instance->bpb.total_sectors_32    = (uint32_t)lba_buffer[32] | 
							((uint32_t)lba_buffer[33] << 8) | 
							((uint32_t)lba_buffer[34] << 16) | 
							((uint32_t)lba_buffer[35] << 24);

	// Переходим к заполнению расширения (union) для FAT12/16
	instance->bpb.ext.fat12_16.drive_number   = lba_buffer[36];
	instance->bpb.ext.fat12_16.reserved1      = lba_buffer[37];
	instance->bpb.ext.fat12_16.boot_signature = lba_buffer[38];

	instance->bpb.ext.fat12_16.volume_id      = (uint32_t)lba_buffer[39] | 
							((uint32_t)lba_buffer[40] << 8) | 
							((uint32_t)lba_buffer[41] << 16) | 
							((uint32_t)lba_buffer[42] << 24);

	// Побайтово копируем массивы строк (Volume Label и FS Type)
	for (int i = 0; i < 11; i++) {
		instance->bpb.ext.fat12_16.volume_label[i] = lba_buffer[43 + i]; // байты 43..53
	}

	for (int i = 0; i < 8; i++) {
		instance->bpb.ext.fat12_16.fs_type[i] = lba_buffer[54 + i];     // байты 54..61
	}

	uint32_t fat1_sector = instance->bpb.reserved_sectors; 
    uint32_t bytes_sec = instance->bpb.bytes_per_sector;
    instance->flash_map.fat1_addr = fat1_sector * bytes_sec;

	if (instance->bpb.sectors_per_fat_16 == 0) {
        
        // --- РАЗДЕЛ FAT32 ---
        instance->type = FAT_TYPE_32;

        // Побайтово заполняем union-расширение для FAT32 (начиная со смещения 36)
        instance->bpb.ext.fat32.sectors_per_fat_32 = (uint32_t)lba_buffer[36] | ((uint32_t)lba_buffer[37] << 8) |
                                                     ((uint32_t)lba_buffer[38] << 16) | ((uint32_t)lba_buffer[39] << 24);
        
        instance->bpb.ext.fat32.ext_flags          = (uint16_t)lba_buffer[40] | ((uint16_t)lba_buffer[41] << 8);
        instance->bpb.ext.fat32.fs_version         = (uint16_t)lba_buffer[42] | ((uint16_t)lba_buffer[43] << 8);
        
        instance->bpb.ext.fat32.root_cluster       = (uint32_t)lba_buffer[44] | ((uint32_t)lba_buffer[45] << 8) |
                                                     ((uint32_t)lba_buffer[46] << 16) | ((uint32_t)lba_buffer[47] << 24);
        
        instance->bpb.ext.fat32.fs_info_sector     = (uint16_t)lba_buffer[48] | ((uint16_t)lba_buffer[49] << 8);
        instance->bpb.ext.fat32.backup_boot_sector = (uint16_t)lba_buffer[50] | ((uint16_t)lba_buffer[51] << 8);
        
        for (int i = 0; i < 12; i++) {
            instance->bpb.ext.fat32.reserved2[i] = lba_buffer[52 + i];
        }

        // Расчёт карты адресов для FAT32
        uint32_t fat2_sector = fat1_sector + instance->bpb.ext.fat32.sectors_per_fat_32;
        instance->flash_map.fat2_addr = fat2_sector * bytes_sec;

        uint32_t total_fat_sectors = instance->bpb.num_fats * instance->bpb.ext.fat32.sectors_per_fat_32;
        uint32_t data_sector = instance->bpb.reserved_sectors + total_fat_sectors;
        
        instance->flash_map.data_addr = data_sector * bytes_sec;
        
        // В FAT32 корневой каталог лежит в области данных. Сохраняем физический адрес его стартового кластера
        uint32_t root_clus = instance->bpb.ext.fat32.root_cluster;
        instance->flash_map.root_dir_addr = instance->flash_map.data_addr + 
                                            ((root_clus - 2) * instance->bpb.sectors_per_cluster * bytes_sec);
        
        instance->flash_map.root_dir_sectors = 0; // Для FAT32 это поле не используется фиксированно
    } 
    else {
        
        // --- РАЗДЕЛ FAT12 / FAT16 ---
        // Побайтово заполняем union-расширение для FAT12/16 (начиная со смещения 36)
        instance->bpb.ext.fat12_16.drive_number   = lba_buffer[36];
        instance->bpb.ext.fat12_16.reserved1      = lba_buffer[37];
        instance->bpb.ext.fat12_16.boot_signature = lba_buffer[38];
        
        instance->bpb.ext.fat12_16.volume_id      = (uint32_t)lba_buffer[39] | ((uint32_t)lba_buffer[40] << 8) |
                                                    ((uint32_t)lba_buffer[41] << 16) | ((uint32_t)lba_buffer[42] << 24);
        
        for (int i = 0; i < 11; i++) {
            instance->bpb.ext.fat12_16.volume_label[i] = lba_buffer[43 + i];
        }
        for (int i = 0; i < 8; i++) {
            instance->bpb.ext.fat12_16.fs_type[i] = lba_buffer[54 + i];
        }

        // Расчёт карты адресов для FAT12/16
        uint32_t fat2_sector = fat1_sector + instance->bpb.sectors_per_fat_16;
        instance->flash_map.fat2_addr = fat2_sector * bytes_sec;

        uint32_t root_sector = fat2_sector + instance->bpb.sectors_per_fat_16;
        instance->flash_map.root_dir_addr = root_sector * bytes_sec;

        instance->flash_map.root_dir_sectors = ((instance->bpb.root_entry_count * 32) + (bytes_sec - 1)) / bytes_sec;

        uint32_t data_sector = root_sector + instance->flash_map.root_dir_sectors;
        instance->flash_map.data_addr = data_sector * bytes_sec;

        // Определяем точный тип (FAT12 или FAT16) по официальному стандарту Microsoft:
        // Считаем общее количество секторов данных на диске
        uint32_t total_sectors = (instance->bpb.total_sectors_16 != 0) ? 
                                  instance->bpb.total_sectors_16 : instance->bpb.total_sectors_32;
        
        uint32_t total_root_and_fat_sectors = instance->bpb.reserved_sectors + 
                                              (instance->bpb.num_fats * instance->bpb.sectors_per_fat_16) + 
                                              instance->flash_map.root_dir_sectors;
        
        uint32_t data_sectors_count = total_sectors - total_root_and_fat_sectors;
        uint32_t total_clusters_count = data_sectors_count / instance->bpb.sectors_per_cluster;

        // Если кластеров меньше 4085 — это FAT12, если от 4085 до 65525 — это FAT16
        if (total_clusters_count < 4085) {
            instance->type = FAT_TYPE_12;
        } else {
            instance->type = FAT_TYPE_16;
        }
    }

    instance->current_dir_cluster = 0;

    return 0; // Успешно скопировано в структуру!
}


uint32_t FAT_GetNextCluster(FAT_Instance_t* instance, uint32_t current_cluster) {
    uint32_t phys_addr = 0;
    
    // === ВЕТКА 1: Чтение для FAT12 ===
    if (instance->type == FAT_TYPE_12) {
        uint8_t buf[2];
        uint32_t fat_offset_bytes = (current_cluster * 3) / 2;
        phys_addr = instance->flash_map.fat1_addr + fat_offset_bytes;
        
        instance->disk_read(buf, phys_addr, 2);
        
        uint16_t raw_entry = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        
        if (current_cluster % 2 == 0) {
            return raw_entry & 0x0FFF; // Четный кластер — берем младшие 12 бит
        } else {
            return raw_entry >> 4;     // Нечетный кластер — берем старшие 12 бит
        }
    }
    // === ВЕТКА 2: Чтение для FAT16 ===
    else if (instance->type == FAT_TYPE_16) {
        uint8_t buf[2];
        uint32_t fat_offset_bytes = current_cluster * 2;
        phys_addr = instance->flash_map.fat1_addr + fat_offset_bytes;
        
        instance->disk_read(buf, phys_addr, 2);
        
        return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    }
    // === ВЕТКА 3: Чтение для FAT32 ===
    else if (instance->type == FAT_TYPE_32) {
        uint8_t buf[4];
        uint32_t fat_offset_bytes = current_cluster * 4;
        phys_addr = instance->flash_map.fat1_addr + fat_offset_bytes;
        
        instance->disk_read(buf, phys_addr, 4);
        
        uint32_t raw_entry = (uint32_t)buf[0] | 
                             ((uint32_t)buf[1] << 8) |
                             ((uint32_t)buf[2] << 16) | 
                             ((uint32_t)buf[3] << 24);
                             
        return raw_entry & 0x0FFFFFFF; // В FAT32 используются только 28 бит
    }
    
    return 0x0FFFFFFF; // Если тип неизвестен, возвращаем универсальный 32-битный конец файла
}

uint32_t FAT_FindFreeCluster(FAT_Instance_t* instance) {
    uint8_t __attribute__((aligned(4))) fat_buf[instance->bpb.bytes_per_sector];
    
    // 1. Узнаем размер таблицы FAT в секторах
    uint32_t fat_size_sectors = (instance->bpb.sectors_per_fat_16 != 0) ? 
                                 instance->bpb.sectors_per_fat_16 : instance->bpb.ext.fat32.sectors_per_fat_32;

    uint32_t current_cluster = 0; // Счётчик кластеров, который мы увеличиваем по мере чтения байт

    // LBA цикл: бежим строго по секторам таблицы FAT от начала до конца
    for (uint32_t sector_idx = 0; sector_idx < fat_size_sectors; sector_idx++) {
        
        // Читаем текущий сектор таблицы FAT целиком в буфер
        uint32_t target_fat_addr = instance->flash_map.fat1_addr + (sector_idx * instance->bpb.bytes_per_sector);
        instance->disk_read(fat_buf, target_fat_addr, instance->bpb.bytes_per_sector);

        // Внутренний цикл: разбираем байты внутри прочитанного сектора
        uint32_t byte_offset = 0;
        while (byte_offset < instance->bpb.bytes_per_sector) {
            uint32_t cluster_value = 0;

            // === ВЕТКА FAT12 ===
            if (instance->type == FAT_TYPE_12) {
                // Защита от стыка: если мы на последнем байте сектора, используем безопасное чтение
                if (byte_offset == (instance->bpb.bytes_per_sector - 1)) {
                    cluster_value = FAT_GetNextCluster(instance, current_cluster);
                    byte_offset += 1; // Стык занимает 1.5 байта, но в этом секторе остался всего 1 байт
                } 
                else {
                    uint16_t raw_entry = (uint16_t)fat_buf[byte_offset] | ((uint16_t)fat_buf[byte_offset + 1] << 8);
                    cluster_value = (current_cluster & 1) ? (raw_entry >> 4) : (raw_entry & 0x0FFF);
                    // Каждые 2 кластера занимают ровно 3 байта. 
                    // Если текущий кластер четный, мы проверили только его, но смещаться по байтам еще рано (сдвиг будет на нечетном).
                    // Для простоты: шагаем по 1.5 байта в среднем.
                    if (current_cluster & 1) {
                        byte_offset += 2; // Шаг после нечетного
                    } else {
                        byte_offset += 1; // Шаг после четного
                    }
                }
            }
            // === ВЕТКА FAT16 ===
            else if (instance->type == FAT_TYPE_16) {
                cluster_value = (uint16_t)fat_buf[byte_offset] | ((uint16_t)fat_buf[byte_offset + 1] << 8);
                byte_offset += 2; // Каждая запись строго 2 байта
            }
            // === ВЕТКА FAT32 ===
            else { 
                cluster_value = ((uint32_t)fat_buf[byte_offset]) | 
                                ((uint32_t)fat_buf[byte_offset + 1] << 8) |
                                ((uint32_t)fat_buf[byte_offset + 2] << 16) | 
                                ((uint32_t)fat_buf[byte_offset + 3] << 24);
                cluster_value &= 0x0FFFFFFF;
                byte_offset += 4; // Каждая запись строго 4 байта
            }

            // Кластеры 0 и 1 зарезервированы системой, их проверять на "свободность" нельзя
            if (current_cluster >= 2 && cluster_value == 0) {
                return current_cluster; // Нашли свободный кластер!
            }

            current_cluster++; // Переходим к следующему номеру кластера
        }
    }

    return 0; // Свободных кластеров во всей таблице FAT не найдено
}

void FAT_WriteClusterValue(FAT_Instance_t* instance, uint32_t cluster, uint32_t value) {
    // Выделяем всего 4 байта в памяти вместо 512!
    uint8_t raw[4] = {0}; 
    uint32_t fat_offset_bytes = 0;

    // === ВЕТКА 1: FAT12 ===
    if (instance->type == FAT_TYPE_12) {
        fat_offset_bytes = (cluster * 3) / 2;
        uint32_t phys_addr_fat1 = instance->flash_map.fat1_addr + fat_offset_bytes;
        uint32_t phys_addr_fat2 = instance->flash_map.fat2_addr + fat_offset_bytes;

        // Читаем 4 байта (захватываем целевую запись и кусочек данных за ней)
        instance->disk_read(raw, phys_addr_fat1, 4);

        // Нам нужны только первые 2 байта из прочитанных 4-х для модификации записи
        uint16_t raw_entry = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
        if (cluster % 2 == 0) {
            raw_entry = (raw_entry & 0xF000) | (value & 0x0FFF);
        } else {
            raw_entry = (raw_entry & 0x000F) | ((value & 0x0FFF) << 4);
        }

        raw[0] = (uint8_t)(raw_entry & 0xFF);
        raw[1] = (uint8_t)((raw_entry >> 8) & 0xFF);

        // Перезаписываем обновленные 4 байта обратно (хвост пишется без изменений)
        instance->disk_write(raw, phys_addr_fat1, 4);
        instance->disk_write(raw, phys_addr_fat2, 4);
    }

    // === ВЕТКА 2: FAT16 ===
    else if (instance->type == FAT_TYPE_16) {
        fat_offset_bytes = cluster * 2;
        uint32_t phys_addr_fat1 = instance->flash_map.fat1_addr + fat_offset_bytes;
        uint32_t phys_addr_fat2 = instance->flash_map.fat2_addr + fat_offset_bytes;

        // Читаем 4 байта (захватываем нужный кластер и следующий за ним)
        instance->disk_read(raw, phys_addr_fat1, 4);

        // Модифицируем только первые 2 байта, отвечающие за наш кластер
        raw[0] = (uint8_t)(value & 0xFF);
        raw[1] = (uint8_t)((value >> 8) & 0xFF);

        // Записываем 4 байта обратно
        instance->disk_write(raw, phys_addr_fat1, 4);
        instance->disk_write(raw, phys_addr_fat2, 4);
    }

    // === ВЕТКА 3: FAT32 ===
    else if (instance->type == FAT_TYPE_32) {
        fat_offset_bytes = cluster * 4;
        uint32_t phys_addr_fat1 = instance->flash_map.fat1_addr + fat_offset_bytes;
        uint32_t phys_addr_fat2 = instance->flash_map.fat2_addr + fat_offset_bytes;

        // Читаем честные 4 байта записи FAT32
        instance->disk_read(raw, phys_addr_fat1, 4);

        uint32_t old_value = ((uint32_t)raw[0]) | ((uint32_t)raw[1] << 8) |
                             ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);

        // Сохраняем зарезервированные верхние 4 бита
        uint32_t final_value = (old_value & 0xF0000000) | (value & 0x0FFFFFFF);

        raw[0] = (uint8_t)(final_value & 0xFF);
        raw[1] = (uint8_t)((final_value >> 8) & 0xFF);
        raw[2] = (uint8_t)((final_value >> 16) & 0xFF);
        raw[3] = (uint8_t)((final_value >> 24) & 0xFF);

        instance->disk_write(raw, phys_addr_fat1, 4);
        instance->disk_write(raw, phys_addr_fat2, 4);
    }
}


static inline bool char_compare_case_insensitive(char req_char, char flash_char) {
    // Переводим в верхний регистр символ из запроса пользователя
    if (req_char >= 'a' && req_char <= 'z') req_char -= 32;
    
    // ДОБАВЬ ЭТУ СТРОЧКУ: Переводим в верхний регистр символ, считанный с флешки!
    if (flash_char >= 'a' && flash_char <= 'z') flash_char -= 32;
    
    return req_char == flash_char;
}

uint8_t FAT_FindFileByLongName(FAT_Instance_t* instance, const char* long_name){
    // Проверяем, что нам передали живые указатели и имя не пустое
    if (instance == NULL || instance->disk_read == NULL || long_name == NULL || long_name[0] == '\0') {
        return 1; // Ошибка: неверные аргументы
    }

    // Сбрасываем параметры файла перед началом нового поиска
    instance->current_file_cluster = 0;
    instance->current_file_size = 0;
    instance->is_file_open = false;

    uint32_t bytes_sec = instance->bpb.bytes_per_sector;
    
    // Переменные для управления обходом каталога
    uint32_t current_cluster = instance->current_dir_cluster;
    uint32_t sector_offset = 0;
    bool is_fixed_root = false;

    // Выясняем, где именно на флешке искать записи
    if (current_cluster == 0) {
        // Мы в корневом каталоге!
        if (instance->type == FAT_TYPE_32) {
            // В FAT32 корень — это обычная цепочка кластеров
            current_cluster = instance->bpb.ext.fat32.root_cluster;
        } else {
            // В FAT12/16 корень — это отдельная фиксированная область секторов
            is_fixed_root = true;
        }
    }

    // Сюда мы будем читать данные с диска (всего 1 сектор)
    uint8_t __attribute__((aligned(4))) sector_buf[bytes_sec];

    char lfn_buffer[256];
    bool lfn_is_valid = false; // Флаг, что мы успешно зафиксировали цепочку LFN-записей

    while ((is_fixed_root && sector_offset < instance->flash_map.root_dir_sectors) || 
           (!is_fixed_root && current_cluster >= 2 && current_cluster < 0x0FFFFFF8)) 
    {
        uint32_t sector_addr = 0;

        if (is_fixed_root) {
            // Случай А: Читаем фиксированный корень FAT12/16
            sector_addr = instance->flash_map.root_dir_addr + (sector_offset * bytes_sec);
            sector_offset++;
        } 
        else {
            // Случай Б: Читаем через цепочку кластеров (подпапки или корень FAT32)
            
            // Проверяем, не прочитали ли мы весь текущий кластер
            if (sector_offset >= instance->bpb.sectors_per_cluster) {
                // Кластер прочитан полностью! Переходим к следующему по таблице FAT
                current_cluster = FAT_GetNextCluster(instance, current_cluster);
                sector_offset = 0; // Сбрасываем счетчик секторов для нового кластера
                
                // Сразу проверяем новый кластер, чтобы не высчитывать неверный адрес
                continue; 
            }

            // Вычисляем физический адрес сектора внутри текущего кластера
            uint32_t cluster_size_bytes = instance->bpb.sectors_per_cluster * bytes_sec;
            uint32_t cluster_start_addr = instance->flash_map.data_addr + ((current_cluster - 2) * cluster_size_bytes);

            sector_addr = cluster_start_addr + (sector_offset * bytes_sec);
            sector_offset++;
        }

        // Читаем вычисленный сектор с диска в буфер
        instance->disk_read(sector_buf, sector_addr, bytes_sec);
        lfn_is_valid = false;

        // Вычисляем, сколько записей помещается в один сектор
        uint16_t entries_per_sector = bytes_sec / 32;

        // Перебираем каждую 32-байтную запись в текущем секторе
        for (uint16_t i = 0; i < entries_per_sector; i++) {
            uint32_t offset = i * 32; // Смещение в байтах от начала буфера сектора

            uint8_t first_byte = sector_buf[offset];      // Первый байт имени (определяет статус записи)
            uint8_t attr       = sector_buf[offset + 11]; // Смещение 11: Атрибуты файла

            // 1. Проверка на конец каталога
            if (first_byte == 0x00) {
                return 2; // Код 2: Свободная запись, и дальше записей НЕТ. Поиск окончен, файла нет.
            }

            // 2. Проверка на удаленный файл
            if (first_byte == 0xE5) {
                continue; // Запись удалена, просто переходим к следующей записи в этом секторе
            }

            // 3. Проверка на LFN-запись (длинное имя)
            // 3. Проверка на LFN-запись (длинное имя)
            if (attr == 0x0F) {
                uint8_t sequence_num = first_byte;

                // Если это стартовая (финальная на диске) запись длинного имени, у неё взведен бит 0x40
                if (sequence_num & 0x40) {
                    sequence_num &= ~0x40; // Очищаем флаг 0x40
                    
                    // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: Полностью зануляем буфер перед сборкой новой строки!
                    for (int b = 0; b < 256; b++) {
                        lfn_buffer[b] = '\0';
                    }
                    
                    uint16_t estimated_len = sequence_num * 13;
                    if (estimated_len > 255) estimated_len = 255;
                    lfn_buffer[estimated_len] = '\0';
                    lfn_is_valid = true; 
                }

                // Извлекаем 13 символов из текущей LFN-записи
                if (sequence_num > 0 && sequence_num <= 20 && lfn_is_valid) {
                    uint16_t string_start_idx = (sequence_num - 1) * 13;

                    for (int c = 0; c < 13; c++) {
                        if (string_start_idx + c < 255) {
                            lfn_buffer[string_start_idx + c] = (char)sector_buf[offset + lfn_offsets[c]];
                        }
                    }
                }

                continue; // Мгновенно уходим на следующую запись (к DOS-записи файла)
            }

            // ================================================================
            // МЫ ДОШЛИ ДО DOS-ЗАПИСИ ФАЙЛА (СВЕРКА ИМЕНИ)
            // ================================================================
            bool file_matched = false;

            // Вариант А: Перед файлом была цепочка LFN, проверяем длинное имя
            if (lfn_is_valid) {
                uint16_t idx = 0;
                bool lfn_match = true;

                // Сверяем строго до конца имени запроса пользователя
                while (long_name[idx] != '\0') {
                    if (!char_compare_case_insensitive(long_name[idx], lfn_buffer[idx])) {
                        lfn_match = false;
                        break;
                    }
                    idx++;
                }
                
                // Если имя совпало, но строка на флешке оказалась длиннее — это чужой файл!
                if (lfn_match && lfn_buffer[idx] != '\0') {
                    lfn_match = false;
                }

                if (lfn_match) {
                    file_matched = true;
                }
            }

            // Вариант Б: Длинное имя не совпало или его не было — проверяем короткое имя (SFN 8.3)
            if (!file_matched) {
                char sfn_name[13]; // Временный буфер для сборки "чистого" DOS-имени
                uint16_t sfn_len = 0;

                // 1. Собираем основное имя (8 байт), отбрасывая пробелы в конце
                for (int n = 0; n < 8; n++) {
                    char c = (char)sector_buf[offset + n];
                    if (c != ' ') {
                        sfn_name[sfn_len++] = c;
                    }
                }

                // 2. Читаем 3 байта расширения
                char ext1 = (char)sector_buf[offset + 8];
                char ext2 = (char)sector_buf[offset + 9];
                char ext3 = (char)sector_buf[offset + 10];

                // Если расширение не пустое, добавляем точку и символы расширения
                if (ext1 != ' ' || ext2 != ' ' || ext3 != ' ') {
                    sfn_name[sfn_len++] = '.';
                    if (ext1 != ' ') sfn_name[sfn_len++] = ext1;
                    if (ext2 != ' ') sfn_name[sfn_len++] = ext2;
                    if (ext3 != ' ') sfn_name[sfn_len++] = ext3;
                }
                sfn_name[sfn_len] = '\0'; // Завершаем собранную строку

                // 3. Прямое посимвольное сравнение собранной DOS-строки с запросом пользователя
                uint16_t idx = 0;
                bool sfn_match = true;
                while (long_name[idx] != '\0' || sfn_name[idx] != '\0') {
                    if (!char_compare_case_insensitive(long_name[idx], sfn_name[idx])) {
                        sfn_match = false;
                        break;
                    }
                    idx++;
                }

                if (sfn_match) {
                    file_matched = true;
                }
            }

            // Финал: если файл подошел по SFN или LFN, забираем его данные
            if (file_matched) {
                uint16_t cluster_low = ((uint16_t)sector_buf[offset + 26]) | ((uint16_t)sector_buf[offset + 27] << 8);
                uint16_t cluster_high = 0;
                
                if (instance->type == FAT_TYPE_32) {
                    cluster_high = ((uint16_t)sector_buf[offset + 20]) | ((uint16_t)sector_buf[offset + 21] << 8);
                }

                instance->current_file_cluster = ((uint32_t)cluster_high << 16) | cluster_low;

                instance->current_file_size = ((uint32_t)sector_buf[offset + 28]) |
                                              ((uint32_t)sector_buf[offset + 29] << 8) |
                                              ((uint32_t)sector_buf[offset + 30] << 16) |
                                              ((uint32_t)sector_buf[offset + 31] << 24);
                
                instance->current_file_attr = attr; 
                instance->current_file_entry_addr = sector_addr + offset;
                instance->is_file_open = true; 

                return 0; // Успех!
            }

            // ГАРАНТИРОВАННЫЙ СБРОС: Если текущий файл не подошел, сбрасываем флаг LFN 
            // перед тем, как перейти к следующей 32-байтной записи в секторе
            lfn_is_valid = false;
        } // Конец цикла по записям сектора
        
        // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: Переносим инкремент сектора строго сюда!
        // Он должен увеличиваться только тогда, когда мы ПОЛНОСТЬЮ разобрали все записи текущего сектора
        sector_offset++; 
    } // Конец цикла по секторам каталога

    return 2; // Код 2: Обошли весь каталог, но файл с таким именем не найден
}

uint8_t FAT_OpenFile(FAT_Instance_t* instance, const char* file_name) {
    if (instance == NULL || instance->disk_read == NULL || file_name == NULL) {
        return 1;
    }

    // --- ПОПЫТКА №1: Ищем файл/папку в текущей директории ---
    uint8_t find_status = FAT_FindFileByLongName(instance, file_name);

    // --- АВТОМАТИЧЕСКИЙ ОТКАТ В КОРЕНЬ ---
    // Если в текущей папке ничего не нашли, но мы сидели не в корне,
    // автоматически возвращаемся в корень диска и пробуем найти элемент там
    if (find_status != 0 && instance->current_dir_cluster != 0) {
        instance->current_dir_cluster = 0; // Сбрасываем папку в корень
        find_status = FAT_FindFileByLongName(instance, file_name); // Пробуем найти снова
    }

    // Если элемент не найден ни там, ни там — возвращаем ошибку 2
    if (find_status != 0) {
        return find_status; 
    }
    
    // --- ОБРАБОТКА РЕЗУЛЬТАТА ПОИСКА ---
    if (instance->current_file_attr == 0x10) {
        // ЭТО ПАПКА! 
        // Переключаем рабочий каталог на её кластер. 
        // Следующие поиски будут происходить уже внутри этой папки.
        instance->current_file_size = 0; 
        instance->current_dir_cluster = instance->current_file_cluster;
        
        // Папку нельзя читать как текстовый файл через функцию ReadFileData
        instance->is_file_open = false; 
    } 
    else {
        // ЭТО ОБЫЧНЫЙ ФАЙЛ!
        // Инициализируем указатели для чтения порциями
        instance->current_file_position = 0;
        instance->current_cluster_pointer = instance->current_file_cluster;
        instance->is_file_open = true;
    }
    
    return 0; // Успешно открыто!
}

uint32_t FAT_ReadFileData(FAT_Instance_t* instance, uint8_t* out_buffer, uint32_t start_byte, uint32_t bytes_to_read) {
    // 1. Проверка «защиты от дурака»
    if (instance == NULL || instance->disk_read == NULL || out_buffer == NULL || bytes_to_read == 0 || !instance->is_file_open) {
        return 0;
    }

    // 2. Если точка старта уже за концом файла — читать нечего
    if (start_byte >= instance->current_file_size) {
        return 0; 
    }

    // 3. Корректируем длину: нельзя прочитать больше, чем осталось до конца файла
    uint32_t max_available = instance->current_file_size - start_byte;
    if (bytes_to_read > max_available) {
        bytes_to_read = max_available;
    }

    // Заготовки констант размеров
    uint32_t bytes_sec = instance->bpb.bytes_per_sector;
    uint32_t cluster_size_bytes = instance->bpb.sectors_per_cluster * bytes_sec;

    // --- УМНАЯ ПЕРЕМОТКА (SEEK) ---
    if (start_byte != instance->current_file_position) {
        instance->current_file_position = start_byte;
        instance->current_cluster_pointer = instance->current_file_cluster; // Возврат на старт файла
        
        // Сколько полных кластеров от начала нужно отсчитать
        uint32_t clusters_to_skip = start_byte / cluster_size_bytes;
        
        for (uint32_t i = 0; i < clusters_to_skip; i++) {
            uint32_t next_cluster = FAT_GetNextCluster(instance, instance->current_cluster_pointer);
            
            // Универсальная проверка на маркеры конца цепочки (EOF) для всех FAT
            if (next_cluster >= 0x0FFFFFF8 || (instance->type == FAT_TYPE_16 && next_cluster >= 0xFFF8) || (instance->type == FAT_TYPE_12 && next_cluster >= 0x0FF8)) {
                return 0; // Цепочка повреждена или файл неожиданно кончился
            }
            instance->current_cluster_pointer = next_cluster;
        }
    }

    uint32_t total_bytes_copied = 0;
    
    // Временный буфер всего на ОДИН сектор (обычно 512 байт), а не 4096!
    uint8_t __attribute__((aligned(4))) tmp_sector_buf[bytes_sec];

    while (total_bytes_copied < bytes_to_read) {
        // 1. Вычисляем текущее смещение внутри КЛАСТЕРА
        uint32_t offset_in_cluster = instance->current_file_position % cluster_size_bytes;
        
        // 2. На основе смещения в кластере находим конкретный СЕКТОР и смещение внутри него
        uint32_t sector_in_cluster = offset_in_cluster / bytes_sec;
        uint32_t byte_offset_in_sector = offset_in_cluster % bytes_sec;

        // 3. Считаем физический адрес этого сектора на диске
        uint32_t cluster_phys_addr = instance->flash_map.data_addr + 
                                     ((instance->current_cluster_pointer - 2) * cluster_size_bytes);
        uint32_t target_sector_addr = cluster_phys_addr + (sector_in_cluster * bytes_sec);

        // 4. Считаем, сколько байт нужно прочитать на этом шаге
        uint32_t bytes_needed = bytes_to_read - total_bytes_copied;
        uint32_t bytes_available_in_sector = bytes_sec - byte_offset_in_sector;
        uint32_t chunk_to_copy = (bytes_needed < bytes_available_in_sector) ? bytes_needed : bytes_available_in_sector;

        // --- УМНАЯ ОПТИМИЗАЦИЯ ЧТЕНИЯ ---
        if (byte_offset_in_sector == 0 && chunk_to_copy == bytes_sec) {
            // Если нам нужно прочесть весь сектор целиком и мы стоим на его начале,
            // читаем данные с флешки НАПРЯМУЮ в память пользователя, без промежуточных буферов!
            instance->disk_read(&out_buffer[total_bytes_copied], target_sector_addr, bytes_sec);
            total_bytes_copied += bytes_sec;
            instance->current_file_position += bytes_sec;
        } 
        else {
            // Если кусочек мелкий или невыровненный, читаем один сектор в темп-буфер
            instance->disk_read(tmp_sector_buf, target_sector_addr, bytes_sec);
            
            // Копируем только полезную часть
            for (uint32_t i = 0; i < chunk_to_copy; i++) {
                out_buffer[total_bytes_copied] = tmp_sector_buf[byte_offset_in_sector + i];
                total_bytes_copied++;
            }
            instance->current_file_position += chunk_to_copy;
        }
        // 5. Проверяем: если мы дочитали текущий кластер до самого конца, 
        // а пользователю нужно передать ещё данные — прыгаем на следующий кластер
        if ((instance->current_file_position % cluster_size_bytes) == 0 && total_bytes_copied < bytes_to_read) {
            uint32_t next_cluster = FAT_GetNextCluster(instance, instance->current_cluster_pointer);

            // Проверка на маркеры конца файла (EOF) для всех типов FAT
            if (next_cluster >= 0x0FFFFFF8 || 
               (instance->type == FAT_TYPE_16 && next_cluster >= 0xFFF8) || 
               (instance->type == FAT_TYPE_12 && next_cluster >= 0x0FF8)) {
                break; // Цепочка кластеров закончилась, выходим из while
            }
            
            instance->current_cluster_pointer = next_cluster;
        }
    } // Конец цикла while

    return total_bytes_copied;

}

uint32_t FAT_WriteFileData(FAT_Instance_t* instance, const uint8_t* in_buffer, uint32_t start_byte, uint32_t bytes_to_write) {
    if (instance == 0 || instance->disk_read == 0 || instance->disk_write == 0 || in_buffer == 0 || bytes_to_write == 0 || !instance->is_file_open) {
        return 0;
    }

    uint32_t bytes_per_sec = instance->bpb.bytes_per_sector;
    uint32_t cluster_size_bytes = instance->bpb.sectors_per_cluster * bytes_per_sec; // 1024

    // Временные указатели для прохода по цепочке
    uint32_t current_cluster = instance->current_file_cluster;
    uint32_t total_bytes_written = 0;
    uint32_t current_pos = start_byte;

    // Сначала проматываем цепочку FAT до того кластера, куда попадает наш start_byte
    uint32_t clusters_to_skip = start_byte / cluster_size_bytes;
    for (uint32_t i = 0; i < clusters_to_skip; i++) {
        current_cluster = FAT_GetNextCluster(instance, current_cluster);
    }

    static uint8_t __attribute__((aligned(4))) current_sector_buffer[512];

    // ГЛАВНЫЙ ЦИКЛ ЗАПИСИ
    while (total_bytes_written < bytes_to_write) {
        uint32_t offset_in_cluster = current_pos % cluster_size_bytes;
        
        // Вычисляем адрес текущего кластера на чипе W25Q
        uint32_t cluster_phys_addr = instance->flash_map.data_addr + ((current_cluster - 2) * cluster_size_bytes);
        
        // Читаем кластер, модифицируем и пишем обратно (наша стандартная схема)
        instance->disk_read(current_sector_buffer, cluster_phys_addr, cluster_size_bytes);
        
        uint32_t bytes_available_in_cluster = cluster_size_bytes - offset_in_cluster;
        uint32_t bytes_needed = bytes_to_write - total_bytes_written;
        uint32_t chunk_to_write = (bytes_needed < bytes_available_in_cluster) ? bytes_needed : bytes_available_in_cluster;
        
        for (uint32_t i = 0; i < chunk_to_write; i++) {
            current_sector_buffer[offset_in_cluster + i] = in_buffer[total_bytes_written + i];
        }
        
        instance->disk_write(current_sector_buffer, cluster_phys_addr, cluster_size_bytes);
        
        total_bytes_written += chunk_to_write;
        current_pos += chunk_to_write;
        
        // ЕСЛИ КЛАСТЕР ЗАКОНЧИЛСЯ, А ДАННЫЕ ЕЩЕ ЕСТЬ
        if (total_bytes_written < bytes_to_write) {
            uint32_t next_cluster = FAT_GetNextCluster(instance, current_cluster);
            
            // Если уперлись в маркер конца файла (>= 0x0FF8), пора выделять НОВЫЙ кластер!
            if (next_cluster >= 0x0FF8) {
                uint32_t free_cluster = FAT_FindFreeCluster(instance);
                if (free_cluster == 0) {
                    break; // Ошибка: флешка физически переполнена!
                }
                
                // 1. Старому кластеру прописываем ссылку на этот новый свободный кластер
                FAT_WriteClusterValue(instance, current_cluster, free_cluster);
                
                // 2. Новому кластеру ставим жесткий маркер конца файла
                FAT_WriteClusterValue(instance, free_cluster, 0x0FFF);
                
                next_cluster = free_cluster;
            }
            current_cluster = next_cluster;
        }
    }

    // ОБНОВЛЯЕМ РАЗМЕР В ПАСПОРТЕ (как на прошлом шаге)
    if (current_pos > instance->current_file_size) {
        instance->current_file_size = current_pos;
    }

    uint32_t entry_sector_addr = instance->current_file_entry_addr & ~(bytes_per_sec - 1);
    uint32_t offset_in_sector = instance->current_file_entry_addr % bytes_per_sec;

    instance->disk_read(current_sector_buffer, entry_sector_addr, bytes_per_sec);
    current_sector_buffer[offset_in_sector + 28] = (uint8_t)(instance->current_file_size & 0xFF);
    current_sector_buffer[offset_in_sector + 29] = (uint8_t)((instance->current_file_size >> 8) & 0xFF);
    current_sector_buffer[offset_in_sector + 30] = (uint8_t)((instance->current_file_size >> 16) & 0xFF);
    current_sector_buffer[offset_in_sector + 31] = (uint8_t)((instance->current_file_size >> 24) & 0xFF);
    current_sector_buffer[offset_in_sector + 22] ^= 0xFF; 

    instance->disk_write(current_sector_buffer, entry_sector_addr, bytes_per_sec);

    return total_bytes_written;
}



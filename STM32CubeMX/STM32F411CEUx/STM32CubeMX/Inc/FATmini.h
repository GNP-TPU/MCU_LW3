#ifndef FAT_MINI_H
#define FAT_MINI_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef enum {
    FAT_TYPE_UNKNOWN = 0,
    FAT_TYPE_12,
    FAT_TYPE_16,
    FAT_TYPE_32,
    FAT_TYPE_EX       // exFAT
} FAT_Type_t;

typedef void (*FAT_ReadFunc_t)(uint8_t* buffer, uint32_t phys_address, uint32_t length);
typedef void (*FAT_WriteFunc_t)(uint8_t* buffer, uint32_t phys_address, uint32_t length);

typedef struct {
    /* --- Базовый BPB (смещение от 11-го байта, длина 25 байт) --- */
    uint16_t bytes_per_sector;     // [11-12] Байт в секторе (обычно 512)
    uint8_t  sectors_per_cluster;  // [13]    Секторов в кластере (у вас 8)
    uint16_t reserved_sectors;     // [14-15] Резервные сектора (обычно 1)
    uint8_t  num_fats;             // [16]    Количество таблиц FAT (обычно 2)
    uint16_t root_entry_count;     // [17-18] Макс. записей в корне (для FAT32 всегда 0)
    uint16_t total_sectors_16;     // [19-20] Всего секторов, если объем < 32МБ
    uint8_t  media_descriptor;     // [21]    Тип носителя (0xF8 для флешек)
    uint16_t sectors_per_fat_16;   // [22-23] Секторов на одну FAT (для FAT32 всегда 0)
    uint16_t sectors_per_track;    // [24-25] Секторов на дорожку (для флешек зануляется)
    uint16_t num_heads;            // [26-27] Количество головок (для флешек зануляется)
    uint32_t hidden_sectors;       // [28-31] Скрытые сектора (LBA начала раздела)
    uint32_t total_sectors_32;     // [32-35] Всего секторов, если объем >= 32МБ

    /* --- Расширение для FAT32 (начинается с 36-го байта) --- */
    union {
        // Если ФС определена как FAT12 или FAT16
        struct __attribute__((packed)){
            uint8_t  drive_number;      // [36] Номер диска для BIOS
            uint8_t  reserved1;         // [37] Резерв
            uint8_t  boot_signature;    // [38] Сигнатура расширения (0x29)
            uint32_t volume_id;         // [39-42] Серийный номер тома
            uint8_t  volume_label[11];  // [43-53] Метка тома (у вас "NO NAME    ")
            uint8_t  fs_type[8];        // [54-61] Строка типа ФС ("FAT12   ")
        } fat12_16;

        // Если ФС определена как FAT32 (расширяет структуру до 64 байта)
        struct __attribute__((packed)){
            uint32_t sectors_per_fat_32;// [36-39] Секторов на одну FAT32
            uint16_t ext_flags;         // [40-41] Флаги зеркалирования FAT
            uint16_t fs_version;        // [42-43] Версия ФС
            uint32_t root_cluster;      // [44-47] Номер первого кластера Root Dir (обычно 2)
            uint16_t fs_info_sector;    // [48-49] Сектор со служебной инфой FSINFO
            uint16_t backup_boot_sector;// [50-51] Сектор с копией Boot-сектора
            uint8_t  reserved2[12];     // [52-63] Зарезервировано
        } fat32;
    } __attribute__((packed)) ext;
    
} __attribute__((packed, aligned(4))) FAT_BPB_t;

typedef struct {
    uint32_t fat1_addr;         // Физический адрес начала таблицы FAT1
    uint32_t fat2_addr;         // Физический адрес начала таблицы FAT2
    uint32_t root_dir_addr;     // Физический адрес начала Корневого Каталога
    uint32_t root_dir_sectors;  // Сколько секторов занимает каталог
    uint32_t data_addr;         // Физический адрес начала Области Данных (Кластер #2)
} FAT_Map_t;

typedef struct {
    FAT_ReadFunc_t  disk_read; // [HAL-слой] У каждого диска здесь будет СВОЯ функция чтения
    FAT_WriteFunc_t disk_write;
    FAT_Type_t      type;      // Тип файловой системы (FAT12, FAT16, FAT32)
    FAT_BPB_t       bpb;       // Ваша структура BPB (память под неё выделяется для каждого диска отдельно!)
    FAT_Map_t       flash_map; // Ваша карта адресов (тоже своя для каждого диска)

    uint32_t current_file_cluster; // Стартовый кластер найденного файла
    uint32_t current_file_size;    // Реальный размер файла в байтах
    bool     is_file_open;         // Флаг: успешно ли найден и открыт файл

    uint32_t current_file_position; // Текущая позиция чтения в байтах (от 0 до file_size)
    uint32_t current_cluster_pointer;// Кластер, в котором мы сейчас находимся

    uint8_t  current_file_attr;  // Сюда запишется атрибут (0x10 для папки, 0x20 для файла)
    uint32_t current_dir_cluster;     // КЛЮЧЕВОЙ МАРКЕР: 0 - корень, иначе - кластер текущей папки

    uint32_t current_file_entry_addr; // Физический адрес 32-байтного DOS-паспорта файла на W25Q
} FAT_Instance_t;

uint8_t     FAT_Mount(FAT_Instance_t* instance);
uint8_t     FAT_OpenFile(FAT_Instance_t* instance, const char* file_name);
uint32_t    FAT_ReadFileData(FAT_Instance_t* instance, uint8_t* out_buffer, uint32_t start_byte, uint32_t bytes_to_read);
uint32_t    FAT_WriteFileData(FAT_Instance_t* instance, const uint8_t* in_buffer, uint32_t start_byte, uint32_t bytes_to_write);

#endif

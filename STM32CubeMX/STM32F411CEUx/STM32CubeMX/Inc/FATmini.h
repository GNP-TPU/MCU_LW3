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
    FAT_TYPE_12_16,
    FAT_TYPE_32,
    FAT_TYPE_EX       // exFAT
} FAT_Type_t;

typedef void (*FAT_ReadFunc_t)(uint32_t sector_address, uint8_t* buffer);
typedef void (*FAT_WriteFunc_t)(uint32_t sector_address, uint8_t* buffer);

typedef struct {
    FAT_ReadFunc_t      DiskRead;
    FAT_WriteFunc_t     DiskWrite;

    FAT_Type_t          FAT_Type;
    char*               FAT_Type_String;
    /*
    |Type|      |Field name|            | Offset    | Size (bytes) |
    */
    uint8_t     JumpBoot[3];            // [0]      | 3 
    uint8_t     OEMName[8];             // [3]      | 8
    uint16_t    BytesPerSector;         // [11]     | 2
    uint8_t     SectorsPerCluster;      // [13]     | 1
    uint16_t    ReservedSectorsCount;   // [14]     | 2
    uint8_t     NumFATs;                // [16]     | 1
    uint16_t    RootEntriesCount;       // [17]     | 2     // FAT32 value 0
    
    uint8_t     MediaDescriptor;        // [21]     | 1
    
    uint16_t    SectorsPerTrack;        // [24]     | 2
    uint16_t    NumHeads;               // [26]     | 2
    uint32_t    HiddenSectors;          // [28]     | 4

    uint16_t    Sign;                   // [510]    | 2     // Always 0xAA55
    
    union{
        struct __attribute__((packed)){
            uint16_t    TotalSectors16;         // [19]     | 2     // FAT32 value 0, > 0xFFFF used TotalSectors32
            uint16_t    FAT_Size_16;            // [22]     | 2     // FAT32 value 0, refer to FAT_Size_32

            uint8_t     DrvNumber;              // [36]     | 1
            uint8_t     BS_Reserved;            // [37]     | 1
            uint8_t     BootSignature;          // [38]     | 1     // Extended boot signature 0x29, indicates that the following 3 fields are present;
            uint32_t    VolID;                  // [39]     | 4
            uint8_t     VolLabel[11];           // [43]     | 11    // Volume label
            uint8_t     FileSystemType[8];      // [54]     | 8     // Not defines FAT type        
        } FAT12_16;

        struct __attribute__((packed)){
            uint32_t    TotalSectors32;         // [32]     | 4     // FAT32 valid value, < 0x10000 used TotalSectors16
            uint32_t    FAT_Size_32;            // [36]     | 4     //  FAT32 value 0, refer to FAT_Size_32

            uint16_t    ExtFlags;               // [40]     | 2     //  Bit3-0: Active FAT starting from 0. Valid when bit7 is 1.
                                                                    //  Bit6-4: Reserved (0).
                                                                    //  Bit7:   0 means that each FAT are active and mirrored. 
                                                                    //          1 means that only one FAT indicated by bit3-0 is active.
                                                                    //  Bit15-8-4: Reserved (0).
            uint16_t    FSVersion;              // [42]     | 2     // FAT32 version
            uint32_t    RootCluster;            // [44]     | 4     // First cluster number
            uint16_t    FSInfo;                 // [48]     | 2     // Sector of FSInfo structer
            uint16_t    BackupBootSector;       // [50]     | 2     // Sector of backup boot sector

            uint8_t     DrvNumber;              // [64]     | 1
            uint8_t     BS_Reserved;            // [65]     | 1
            uint8_t     BootSignature;          // [66]     | 1     // Extended boot signature 0x29, indicates that the following 3 fields are present;
            uint32_t    VolID;                  // [67]     | 4
            uint8_t     VolLabel[11];           // [71]     | 11    // Volume label
            uint8_t     FileSystemType[8];      // [82]     | 8     // Not defines FAT type        
        } FAT32;

        struct __attribute__((packed)){
            /*TODO*/
        } EXFAT;

    } __attribute__((packed)) FAT;

    uint32_t TotalSectors;

    uint32_t FAT_StartSector;
    uint32_t FAT_StartAddress;
    uint32_t FAT_NumSectors;

    uint32_t RootDirectory_StartSector;
    uint32_t RootDirectory_StartAddress;
    uint32_t RootDirectory_NumSectors;

    uint32_t Cluster2_StartSector;
    uint32_t Cluster2_StartAddress;

    uint32_t CountOfClusters;

    struct {
        uint8_t     Attr;               // Аттрибут директории
        uint32_t    FirstCluster;       // Первый кластер найденного файла
        uint32_t    CurrentCluster;     // Кластер, на котором сейчас находится указатель чтения
        uint32_t    FileSize;           // Размер файла в байтах
        uint32_t    CurrentPosition;    // Текущая позиция чтения (в байтах от начала файла)
    } __attribute__((packed, aligned(4))) Directory;

} __attribute__((packed, aligned(4))) FATmini_t;



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

uint8_t     FAT_Mount(FATmini_t* instance);
bool        FAT_OpenFile(FATmini_t* FAT_Struct, char* FileName);
bool        FAT_OpenDirectory(FATmini_t* FAT_Struct, char* DirName);
void        FAT_ReturnRootDirectory(FATmini_t* FAT_Struct);

bool        FAT_ReadFile(FATmini_t* FAT_Struct, uint8_t* OutBuffer, uint32_t StartByte, uint32_t Length);

#endif

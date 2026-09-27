#ifndef ST7789_H
#define ST7789_H
//==============================================================================
#include <stm32f4xx.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "FreeSans9pt7b.h"
//==============================================================================
#include "RCC.h"
//==============================================================================
#define ST77xx_CS_HIGH() 				GPIOA->ODR |= GPIO_ODR_OD4
#define ST77xx_CS_LOW() 				GPIOA->ODR &= ~GPIO_ODR_OD4

#define	ST77xx_DC_HIGH()				GPIOA->ODR |= GPIO_ODR_OD3
#define	ST77xx_DC_LOW()  				GPIOA->ODR &= ~GPIO_ODR_OD3

#define ST77xx_RST_HIGH() 			    GPIOA->ODR |= GPIO_ODR_OD2
#define ST77xx_RST_LOW() 				GPIOA->ODR &= ~GPIO_ODR_OD2
//==============================================================================
#define ST77xx_X_Start          0
#define ST77xx_Y_Start          0
//==============================================================================
//#define ST7735_Cmd_NOP        0x00
#define ST77xx_Cmd_SWRESET      0x01
//#define ST7735_Cmd_RDDID      0x04
//#define ST7735_Cmd_RDDST      0x09
#define ST77xx_Cmd_SLPIN        0x10
#define ST77xx_Cmd_SLPOUT       0x11
#define ST77xx_Cmd_PTLON        0x12
#define ST77xx_Cmd_NORON        0x13
#define ST77xx_Cmd_INVOFF       0x20
#define ST77xx_Cmd_INVON        0x21
#define ST77xx_Cmd_GAMSET       0x26
#define ST77xx_Cmd_DISPOFF      0x28
#define ST77xx_Cmd_DISPON       0x29
#define ST77xx_Cmd_CASET        0x2A
#define ST77xx_Cmd_RASET        0x2B
#define ST77xx_Cmd_RAMWR        0x2C
//#define ST7735_Cmd_RAMRD        0x2E
#define ST77xx_Cmd_PTLAR        0x30
#define ST77xx_Cmd_COLMOD       0x3A
#define Cmd_VSCRDEF             0x33
#define ST77xx_Cmd_MADCTL       0x36    // Memory data access control 
#define ST77xx_Cmd_VSCSAD       0x37
#define ST7735_Cmd_FRMCTR1      0xB1    // Frame Rate Control in normal mode
#define ST7735_Cmd_FRMCTR2      0xB2    // Frame Rate Control in idle mode
#define ST7735_Cmd_FRMCTR3      0xB3    // Frame Rate Control in partial mode
#define ST7735_Cmd_INVCTR       0xB4
#define ST7735_Cmd_DISSET5      0xB6    // Display Function set 5 
#define ST7735_Cmd_PWCTR1       0xC0    // Power control 1 
#define ST7735_Cmd_PWCTR2       0xC1    // Power control 2 
#define ST7735_Cmd_PWCTR3       0xC2    // Power control 3 
#define ST7735_Cmd_PWCTR4       0xC3    // Power control 4 
#define ST7735_Cmd_PWCTR5       0xC4    // Power control 5 
#define ST7735_Cmd_VMCTR1       0xC5    // VCOM Control 1
//==============================================================================
#define ST7789_Cmd_MADCTL_MY    0x80
#define ST7789_Cmd_MADCTL_MX    0x40
#define ST7789_Cmd_MADCTL_MV    0x20
#define ST7789_Cmd_MADCTL_ML    0x10
#define ST7789_Cmd_MADCTL_RGB   0x00
//==============================================================================
#define ST7789_Cmd_RDID1        0xDA
#define ST7789_Cmd_RDID2        0xDB
#define ST7789_Cmd_RDID3        0xDC
#define ST7789_Cmd_RDID4        0xDD
//==============================================================================
#define ST7735_ColorMode_12bit  0x03
#define ST7735_ColorMode_16bit  0x05
#define ST7735_ColorMode_18bit  0x06
//==============================================================================
#define ST77xx_MADCTL_MY        0x80
#define ST77xx_MADCTL_MX        0x40
#define ST77xx_MADCTL_MV        0x20
#define ST77xx_MADCTL_ML        0x10
#define ST77xx_MADCTL_BGR       0x08
#define ST77xx_MADCTL_MH        0x04
//==============================================================================
#define ST7789_ColorMode_65K    0x50
#define ST7789_ColorMode_262K   0x60
#define ST7789_ColorMode_12bit  0x03
#define ST7789_ColorMode_16bit  0x05
#define ST7789_ColorMode_18bit  0x06
#define ST7789_ColorMode_16M    0x07
//==============================================================================
#define RGB565(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

#define RED      			0xF800
#define DARK_RED     	    0x7800
#define GREEN    			0x07E0
#define DARK_GREEN   	    0x03E0
#define BLUE     			0x001F
#define DARK_BLUE           0x000F
#define DEEP_TEAL           0x23D2
#define PLUM                0x710E
#define NAVY                0x0110
#define CYAN                0x07FF
#define MAGENTA             0xF81F
#define ORANGE              0xFD20
#define PINK                0xF814
#define PURPLE              0x780F
#define MINT                0x8672
#define OLIVE               0x7BE0
#define YELLOW              0xFFE0
#define LIGHT_GRAY          0xC618
#define GRAY                0x8410
#define DARK_GRAY           0x31A6
#define BLACK               0x0000
#define WHITE               0xFFFF
//==============================================================================
typedef enum {
	CHIP_ST7735,
	CHIP_ST7789
} ST77xx_ChipType;

// 0 PORTRAIT_FLIPPED
// 3 PORTRAIT
// 5 LANDSCAPE_FLIPPED
// 6 LANDSCAPE
typedef enum{
    PORTRAIT_FLIPPED = 0,
    PORTRAIT = 3,
    LANDSCAPE_FLIPPED = 5,
    LANDSCAPE = 6
}ST77xx_DisplayRotation;

typedef struct{
    ST77xx_ChipType     ChipType;

    uint32_t            ST77xx_Width;
    uint32_t            ST77xx_Height;

    ST77xx_DisplayRotation CurrentRotation;

    void                (*ST77xx_CS)(bool); 
    void                (*ST77xx_DC)(bool); 
    void                (*ST77xx_RST)(bool); 
    void                (*ST77xx_SPI_Write)(uint8_t);
}ST77xx_t;


void ST77xx_GPIO_Init(void);

void ST77xx_WriteData(ST77xx_t*, uint8_t);
void ST77xx_WriteCmd(ST77xx_t*, uint8_t);
void ST77xx_HardReset(ST77xx_t*);
void ST77xx_SleepMode(ST77xx_t*, uint8_t);
void ST77xx_ColorModeSet(ST77xx_t*, uint8_t);
void ST77xx_MemAccessModeSet(ST77xx_t*, uint8_t, uint8_t, uint8_t, uint8_t);
void ST77xx_InversionMode(ST77xx_t*, uint8_t);
void ST77xx_DrawRect(ST77xx_t*, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void ST77xx_FillRectGrad(ST77xx_t*, int16_t, int16_t, int16_t, int16_t, uint16_t);
void ST77xx_FillRect(ST77xx_t*, int16_t, int16_t, int16_t, int16_t, uint16_t);
void ST77xx_SetWindow(ST77xx_t*, uint16_t, uint16_t, uint16_t, uint16_t);
void ST77xx_RamWrite(ST77xx_t*, uint16_t, uint32_t);
void ST77xx_RamWriteFast(ST77xx_t* stDriver, uint16_t* pBuff, uint32_t Len);
void ST77xx_ColumnSet(ST77xx_t*, uint16_t, uint16_t);
void ST77xx_RowSet(ST77xx_t*, uint16_t, uint16_t);
void ST77xx_FillScreen(ST77xx_t*, uint16_t);
void ST77xx_DisplayPower(ST77xx_t*, uint8_t);
void ST77xx_Init(ST77xx_t*);
void ST77xx_ChangeRotation(ST77xx_t*);
void ST77xx_DrawPixel(ST77xx_t*, uint16_t, uint16_t, uint16_t);
void ST77xx_Draw_GFX_Char(ST77xx_t*, char c, int16_t x, int16_t y, uint16_t text_color);
void ST77xx_Draw_GFX_String_Centered(ST77xx_t*, const char* str, uint8_t grid_x, uint8_t grid_y, uint16_t text_color);
void ST77xx_ScrollModeArea(ST77xx_t*, uint16_t, uint16_t, uint16_t, uint16_t);
void ST77xx_Scroll(ST77xx_t*, uint16_t, uint16_t);
void ST77xx_DrawLine(ST77xx_t*, int16_t, int16_t, int16_t, uint16_t);
void ST77xx_Draw_Mono_Icon(ST77xx_t*, uint16_t x, uint16_t y, const uint8_t* bitmap, uint16_t icon_width, uint16_t icon_height, uint16_t icon_color, uint16_t bg_color);
void delay(long int);
//==============================================================================
#endif

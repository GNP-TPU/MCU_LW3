#include "Interrupt_Handlers.h"

#define CMD_LENGTH 4

uint8_t uart_cmd[CMD_LENGTH];
uint8_t data_available = 0;
uint8_t uart_cmd_pointer = 0;

void USART1_IRQHandler(void){
	if(USART1->SR & USART_SR_RXNE){
		uint8_t byte = USART1->DR;

		uart_cmd[uart_cmd_pointer++] = byte;

		if(uart_cmd_pointer >= CMD_LENGTH){
			data_available = 1;
			uart_cmd_pointer = 0;
		}
		


	}
	if(USART1->SR & USART_SR_ORE){
		uint8_t byte_error = USART1->DR;							
	}
}



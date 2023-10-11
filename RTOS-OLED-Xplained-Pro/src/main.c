/************************************************************************/
/* includes                                                             */
/************************************************************************/

#include <asf.h>
#include <stdlib.h>
#include <string.h>
#include "conf_board.h"
#include "gfx_mono_ug_2832hsweg04.h"
#include "gfx_mono_text.h"
#include "sysfont.h"

/************************************************************************/
/* IOS                                                                  */
/************************************************************************/

#define BTN_PIO PIOA
#define BTN_PIO_ID ID_PIOA
#define BTN_PIO_PIN 11
#define BTN_PIO_PIN_MASK (1 << BTN_PIO_PIN)

#define BZZ_PIO PIOA
#define BZZ_PIO_ID ID_PIOA
#define BZZ_PIO_PIN 4
#define BZZ_PIO_PIN_MASK (1 << BZZ_PIO_PIN)


#define NOTE_B5  988
#define NOTE_E6  1319
/************************************************************************/
/* prototypes and types                                                 */
/************************************************************************/

void btn_init(void);
void RTT_init(float freqPrescale, uint32_t IrqNPulses, uint32_t rttIRQSource);
void BZZ_init(void);
void tone(int freq, int time);
/************************************************************************/
/* rtos vars                                                            */
/************************************************************************/

volatile int seed = -1;

/* Semaphore for button */
SemaphoreHandle_t xBtnSemaphore;

/* Queue with number of coins meant to be given */
QueueHandle_t xQueueCoins;

/************************************************************************/
/* RTOS application funcs                                               */
/************************************************************************/
#define TASK_OLED_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_OLED_STACK_PRIORITY            (tskIDLE_PRIORITY)

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,  signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName) {
	printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
	for (;;) {	}
}

extern void vApplicationIdleHook(void) { }

extern void vApplicationTickHook(void) { }

extern void vApplicationMallocFailedHook(void) {
	configASSERT( ( volatile void * ) NULL );
}

/************************************************************************/
/* handlers / callbacks                                                 */
/************************************************************************/

void but_callback(void) {
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	xSemaphoreGiveFromISR(xBtnSemaphore, &xHigherPriorityTaskWoken);	
}


/************************************************************************/
/* TASKS                                                                */
/************************************************************************/


static void task_debug(void *pvParameters) {
	gfx_mono_ssd1306_init();

	for (;;) {
		gfx_mono_draw_filled_circle(10,10,4,1,GFX_WHOLE);
		vTaskDelay(150);
		gfx_mono_draw_filled_circle(10,10,4,0,GFX_WHOLE);
		vTaskDelay(150);
	}
}

static void task_coins(void *pvParameters){
	for(;;){
		if(xSemaphoreTake(xBtnSemaphore,0)){
			if(seed == -1){
				seed = rtt_read_timer_value(RTT);
				printf("seed: %d\n",seed);
				srand(seed);
			}
			uint32_t coins = rand()%3+1;
			printf("coins: %d\n", coins);
			xQueueSend(xQueueCoins, &coins, 10);
		}	
		vTaskDelay(200);
	}
}

static void task_play(void *pvParameters){
	
	uint32_t coins = rand()%3+1;
	for(;;){
		if(xQueueReceive(xQueueCoins, &coins, (TickType_t) 0)){
			for (int i=0; i<coins; i++){
				tone(NOTE_B5,  80);
				tone(NOTE_E6, 640);
			}
		}
		vTaskDelay(200);
	}
}

/************************************************************************/
/* funcoes                                                              */
/************************************************************************/

void btn_init(void) {
	// Inicializa clock do periférico PIO responsavel pelo botao
	pmc_enable_periph_clk(BTN_PIO_ID);

	// Configura PIO para lidar com o pino do botão como entrada
	// com pull-up
	pio_configure(BTN_PIO, PIO_INPUT, BTN_PIO_PIN_MASK, PIO_PULLUP | PIO_DEBOUNCE);
	pio_set_debounce_filter(BTN_PIO, BTN_PIO_PIN_MASK, 60);
	
	// Configura interrupção no pino referente ao botao e associa
	// função de callback caso uma interrupção for gerada
	// a função de callback é a: but_callback()
	pio_handler_set(BTN_PIO,
	BTN_PIO_ID,
	BTN_PIO_PIN_MASK,
	PIO_IT_FALL_EDGE,
	but_callback);

	// Ativa interrupção e limpa primeira IRQ gerada na ativacao
	pio_enable_interrupt(BTN_PIO, BTN_PIO_PIN_MASK);
	pio_get_interrupt_status(BTN_PIO);

	// Configura NVIC para receber interrupcoes do PIO do botao
	// com prioridade 4 (quanto mais próximo de 0 maior)
	NVIC_EnableIRQ(BTN_PIO_ID);
	NVIC_SetPriority(BTN_PIO_ID, 4); // Prioridade 4
}

void BZZ_init(){
	// Inicializa clock do periférico PIO responsavel pelo buzzer
	pmc_enable_periph_clk(BTN_PIO_ID);
	pio_configure(BZZ_PIO, PIO_OUTPUT_0, BZZ_PIO_PIN_MASK, PIO_DEFAULT);
}

void RTT_init(float freqPrescale, uint32_t IrqNPulses, uint32_t rttIRQSource) {

	uint16_t pllPreScale = (int)(((float)32768) / freqPrescale);

	rtt_sel_source(RTT, false);
	rtt_init(RTT, pllPreScale);

	if (rttIRQSource & RTT_MR_ALMIEN) {
		uint32_t ul_previous_time;
		ul_previous_time = rtt_read_timer_value(RTT);
		while (ul_previous_time == rtt_read_timer_value(RTT))
		;
		rtt_write_alarm_time(RTT, IrqNPulses + ul_previous_time);
	}

	/* config NVIC */
	NVIC_DisableIRQ(RTT_IRQn);
	NVIC_ClearPendingIRQ(RTT_IRQn);
	NVIC_SetPriority(RTT_IRQn, 4);
	NVIC_EnableIRQ(RTT_IRQn);

	/* Enable RTT interrupt */
	if (rttIRQSource & (RTT_MR_RTTINCIEN | RTT_MR_ALMIEN))
	rtt_enable_interrupt(RTT, rttIRQSource);
	else
	rtt_disable_interrupt(RTT, RTT_MR_RTTINCIEN | RTT_MR_ALMIEN);
}

static void configure_console(void) {
	const usart_serial_options_t uart_serial_options = {
		.baudrate = CONF_UART_BAUDRATE,
		.charlength = CONF_UART_CHAR_LENGTH,
		.paritytype = CONF_UART_PARITY,
		.stopbits = CONF_UART_STOP_BITS,
	};

	/* Configure console UART. */
	stdio_serial_init(CONF_UART, &uart_serial_options);

	/* Specify that stdout should not be buffered. */
	setbuf(stdout, NULL);
}

void tone(int freq, int time){
	int per = 500000/freq;
	int t = (freq*time)/1000;
	
	for(int i = 0; i <= t; i++){
		pio_set(BZZ_PIO, BZZ_PIO_PIN_MASK);
		delay_us(per);
		pio_clear(BZZ_PIO,BZZ_PIO_PIN_MASK);
		delay_us(per);
	}
}

/************************************************************************/
/* main                                                                 */
/************************************************************************/
int main(void) {
	/* Initialize the SAM system */
	sysclk_init();
	board_init();
	
	/* Initialize the console uart */
	configure_console();
	
	if (xTaskCreate(task_debug, "debug", TASK_OLED_STACK_SIZE, NULL,
	TASK_OLED_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create debug task\r\n");
	}
	
	/* Attempts to create a semaphore */
	xBtnSemaphore = xSemaphoreCreateBinary();
	if(xBtnSemaphore == NULL)
	printf("Falha em criar semaforo\n");
	
	/* Creates a Queue with 32 spaces, for integers */
	xQueueCoins = xQueueCreate(32, sizeof(uint32_t));
	if(xQueueCoins == NULL)
	printf("Falha em criar a queue\n");
	
	if(xTaskCreate(task_coins, "coins", TASK_OLED_STACK_SIZE, NULL, TASK_OLED_STACK_PRIORITY, NULL) != pdPASS){
		printf("Failed to create coins task");	
	}
	
	if(xTaskCreate(task_play, "plays", TASK_OLED_STACK_SIZE, NULL, TASK_OLED_STACK_PRIORITY+1, NULL) != pdPASS){
		printf("Failed to create coins task");
	}
	
	RTT_init(3000, 1000000,0);
	btn_init();
	BZZ_init();
	/* Start the scheduler. */
	vTaskStartScheduler();

	/* RTOS não deve chegar aqui !! */
	while(1){}

	/* Will only get here if there was insufficient memory to create the idle task. */
	return 0;
}

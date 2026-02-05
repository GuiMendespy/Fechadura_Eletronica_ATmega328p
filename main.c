/*
 * MiniProjetoVirtus.c
 *
 * Created: 24/01/2026
 * Author : guilh
 */

#define F_CPU 16000000UL
#define BAUD 9600
#define MYUBRR (F_CPU/16/BAUD-1)

#include <avr/io.h>
#include <util/delay.h>
#include <string.h>
#include <avr/interrupt.h>

/* ================= CONFIGURAÇÕES ================= */

#define SENHA_MESTRA "1234"
#define TAM_SENHA 4

#define LED_PINVERD PB5
#define LED_PINVERM PB4
#define SERVO_PIN   PB1

volatile uint8_t eventoPB2 = 0;
volatile uint8_t eventoPB3 = 0;
volatile uint8_t ultimoEstadoPB;
volatile travapb2 = 0;
volatile travapb3 = 0;

/* ================= VARIÁVEIS ================= */

char senha_digitada[TAM_SENHA + 1];
char senha_correta[TAM_SENHA + 1] = "";
uint8_t senha_comparada = 0;
uint8_t indice_senha = 0;
uint8_t modo_cadastro = 0;

char mapa[4][3] = {
	{'1','2','3'},
	{'4','5','6'},
	{'7','8','9'},
	{'*','0','#'}
};

typedef enum {
	SENHA_ANTERIOR,
	SENHA_NOVA
} estado_t;

estado_t estado = SENHA_ANTERIOR;

/* ================= PROTÓTIPOS ================= */

void USART_Init(unsigned int ubrr);
void USART_Transmit(char data);
void USART_SendString(const char *str);

void lcd_init(void);
void lcd_command(unsigned char cmd);
void lcd_char(unsigned char data);
void lcd_string(const char* str);

void init_servo(void);
void posicionar_servo(uint16_t angulo);

char* teclado(void);

/* ================= SERVO ================= */

void init_servo(void) {
	DDRB |= (1 << SERVO_PIN);

	TCCR1A = (1 << COM1A1) | (1 << WGM11);
	TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11);

	ICR1 = 39999;
	posicionar_servo(0);
}

void posicionar_servo(uint16_t angulo) {
	OCR1A = ((uint32_t)angulo * 2000) / 180 + 1000;
}

/* ================= USART ================= */
void USART_Init(unsigned int ubrr) {
	UBRR0H = (unsigned char)(ubrr >> 8);
	UBRR0L = (unsigned char)ubrr;
	UCSR0B = (1 << TXEN0) | (1 << RXEN0);
	UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void USART_Transmit(char data) {
	while (!(UCSR0A & (1 << UDRE0)));
	UDR0 = data;
}

void USART_SendString(const char *str) {
	while (*str) USART_Transmit(*str++);
}

/* ================= LCD ================= */

void lcd_pulse_en(void) {
	PORTC |= (1 << PC1);
	_delay_us(1);
	PORTC &= ~(1 << PC1);
	_delay_us(100);
}

void lcd_send(unsigned char val, uint8_t is_data) {

	// Envia nibble alto (bits 7..4 ? PC5..PC2)
	PORTC = (PORTC & 0x03) | ((val >> 2) & 0x3C);
	if (is_data) PORTC |= (1 << PC0);
	else PORTC &= ~(1 << PC0);
	lcd_pulse_en();

	// Envia nibble baixo (bits 3..0 ? PC5..PC2)
	PORTC = (PORTC & 0x03) | ((val << 2) & 0x3C);
	if (is_data) PORTC |= (1 << PC0);
	else PORTC &= ~(1 << PC0);
	lcd_pulse_en();

	_delay_ms(2);
}


void lcd_command(unsigned char cmd) { lcd_send(cmd, 0); }
void lcd_char(unsigned char data)   { lcd_send(data, 1); }

void lcd_init(void) {
	DDRC |= 0x3F;
	_delay_ms(50);
	lcd_command(0x02);
	lcd_command(0x28);
	lcd_command(0x0C);
	lcd_command(0x01);
	_delay_ms(5);
}

void lcd_string(const char* str) {
	while (*str) lcd_char(*str++);
}

/* ================= TECLADO ================= */

char* teclado(void) {

	for (uint8_t linha = 0; linha < 4; linha++) {

		PORTB |= (1 << PB0);
		PORTD |= (1 << PD7) | (1 << PD6) | (1 << PD5);

		if (linha == 0) PORTB &= ~(1 << PB0);
		else if (linha == 1) PORTD &= ~(1 << PD7);
		else if (linha == 2) PORTD &= ~(1 << PD6);
		else if (linha == 3) PORTD &= ~(1 << PD5);

		_delay_us(50);

		int8_t col = -1;
		uint8_t bit;

		if (!(PIND & (1 << PD4))) { col = 0; bit = PD4; }
		else if (!(PIND & (1 << PD3))) { col = 1; bit = PD3; }
		else if (!(PIND & (1 << PD2))) { col = 2; bit = PD2; }

		if (col != -1) {
			_delay_ms(30);
			if (!(PIND & (1 << bit))) {

				char tecla = mapa[linha][col];
				

				if (tecla == '#') {
					senha_digitada[indice_senha] = '\0';
					indice_senha = 0;
					_delay_ms(500);
					return senha_digitada;
				}
				else if (tecla == '*') {
					indice_senha = 0;
					lcd_command(0x01);
					lcd_string("Senha:");
					lcd_command(0xC0);
				}
				else if (indice_senha < TAM_SENHA) {
					senha_digitada[indice_senha++] = tecla;
					lcd_char('*');
					USART_Transmit(tecla);
				}

				while (!(PIND & (1 << bit)));
				_delay_ms(50);
			}
		}
	}
	return NULL;
}

/* ================= INTERRUPÇÃO ================= */

ISR(PCINT0_vect) {
	uint8_t estadoAtual = PINB;
	uint8_t mudanca = estadoAtual ^ ultimoEstadoPB;

	if (mudanca & (1 << PB2)) {
		if (!(estadoAtual & (1 << PB2)) && (travapb2 == 0)) {
			if(eventoPB2 == 0){
				eventoPB2 = 1;
				travapb2 = 1;
			}
		}
	}

	if (mudanca & (1 << PB3)) {
		if (!(estadoAtual & (1 << PB3)) && (travapb3 == 0)) {
			if(eventoPB3 == 0){
				eventoPB3 = 1;
				travapb3 = 1;
			}
		}
	}

	ultimoEstadoPB = estadoAtual;
}

/* ================= MAIN ================= */

int main(void) {
	
	DDRD &= ~((1 << PD4) | (1 << PD3) | (1 << PD2));
	PORTD |= (1 << PD4) | (1 << PD3) | (1 << PD2);

	DDRB |= (1 << PB0) | (1 << LED_PINVERD) | (1 << LED_PINVERM);
	DDRD |= (1 << PD7) | (1 << PD6) | (1 << PD5);

	DDRB &= ~((1 << PB2) | (1 << PB3));
	PORTB |= (1 << PB2) | (1 << PB3);

	PCICR |= (1 << PCIE0);
	PCMSK0 |= (1 << PCINT2) | (1 << PCINT3);
	ultimoEstadoPB = PINB;
	sei();

	USART_Init(MYUBRR);
	init_servo();
	lcd_init();

	lcd_string("Senha:");
	lcd_command(0xC0);

	char *buf = NULL;
	USART_SendString("\r\nConexao Estabelecida!\r\n");
	USART_SendString("\r\nDigite Senha: \r\n");

	while (1) {
		
		PORTB |= (1 << LED_PINVERM);

		if (eventoPB2) {
			USART_SendString("\r\nInterrupcao no pb2!\r\n");
			
			lcd_command(0x01);
			lcd_string("ABERTO!");
			
			PORTB &= ~(1 << LED_PINVERM);
			PORTB |= (1 << LED_PINVERD);

			posicionar_servo(180);
			_delay_ms(3000); // Porta fica aberta por 3 segundos
			posicionar_servo(0);

			PORTB &= ~(1 << LED_PINVERD);
			
			// --- NOVO RECURSO: AGUARDAR SOLTAR O BOTÃO ---
			// Enquanto o pino PB2 estiver em nível baixo (pressionado), o código "trava" aqui
			while (!(PINB & (1 << PB2))) {
				_delay_ms(10); // Pequena espera para debounce ao soltar
			}
			
			// Agora que o botão foi solto, limpamos a flag para permitir um novo clique
			eventoPB2 = 0;
			travapb2 = 0;
			
			_delay_ms(200); // Tempo de segurança adicional
			lcd_command(0x01);
			lcd_string("Senha:");
			lcd_command(0xC0);
		}

		if (eventoPB3) {
			USART_SendString("\r\nInterrupcao no pb3!\r\n");

			modo_cadastro = 1;
			estado = SENHA_ANTERIOR;

			buf = NULL;
			if(buf == NULL){
				USART_SendString("\r\n buf nulo\r\n");
			}else{
				USART_SendString(buf);
			}
			indice_senha = 0;
			senha_digitada[0] = '\0';
			USART_SendString("\r\n Digite Senha Anterior: \r\n");

			lcd_command(0x01);
			lcd_string("Senha Anterior:");
			lcd_command(0xC0);
			
			eventoPB3 = 0;
			travapb3 = 0;
			_delay_ms(1000);
			continue;
		}
		buf = teclado();
		if (buf == NULL ){
			continue;
		}
		if (senha_correta != NULL){
			senha_comparada = 1;
		}else{
			senha_comparada = 0;
		}
		
		switch (estado) {

			case SENHA_ANTERIOR:

				if (!modo_cadastro) {
					if(senha_comparada == 1){
						if (!strcmp(buf, SENHA_MESTRA) ||
							!strcmp(buf, senha_correta)) {
							USART_SendString(SENHA_MESTRA);
							USART_SendString(senha_correta);
							USART_SendString("\r\nPorta Aberta! \r\n");
							
							USART_SendString(buf);
							lcd_command(0x01);
							lcd_string("ABERTO!");

							PORTB &= ~(1 << LED_PINVERM);
							PORTB |= (1 << LED_PINVERD);

							posicionar_servo(180);
							_delay_ms(3000);
							posicionar_servo(0);
							
							USART_SendString("\r\nPorta Fechada!\r\n");
							PORTB &= ~(1 << LED_PINVERD);
						}else{
							lcd_command(0x01);
							lcd_string("SENHA ERRADA!");
							USART_SendString("\r\nSenha incorreta!, tente novamente \r\n");
							USART_SendString("\r\nDigite Senha: \r\n");
							_delay_ms(1000);
						}
					}else if((!strcmp(buf, SENHA_MESTRA))){
						USART_SendString(SENHA_MESTRA);
						USART_SendString(senha_correta);
						USART_SendString("\r\nPorta Aberta! \r\n");
						
						USART_SendString(buf);
						lcd_command(0x01);
						lcd_string("ABERTO!");

						PORTB &= ~(1 << LED_PINVERM);
						PORTB |= (1 << LED_PINVERD);

						posicionar_servo(180);
						_delay_ms(3000);
						posicionar_servo(0);
						
						USART_SendString("\r\nPorta Fechada!\r\n");
						PORTB &= ~(1 << LED_PINVERD);
					}else{
						lcd_command(0x01);
						lcd_string("ABERTO!");
						USART_SendString("\r\nSenha incorreta!, tente novamente \r\n");
						USART_SendString("\r\nDigite Senha: \r\n");
						_delay_ms(1000);
					}
					
					lcd_command(0x01);
					lcd_string("Senha:");
					lcd_command(0xC0);
				}
				else {
	
					if (buf != NULL){
						if (!strcmp(buf, SENHA_MESTRA) ||
							!strcmp(buf, senha_correta)) {

							estado = SENHA_NOVA;

							lcd_command(0x01);
							lcd_string("Senha Nova:");
							lcd_command(0xC0);
							USART_SendString("\r\nSenha Anterior Certa! \r\n");
							USART_SendString("\r\nDigite Nova senha:  \r\n");
							
						}
						else {
							modo_cadastro = 0;
							lcd_command(0x01);
							lcd_string("Senha:");
							lcd_command(0xC0);
							
						}
					}
				}
				break;

			case SENHA_NOVA:
				strcpy(senha_correta, buf);
				if(senha_correta != NULL){
				USART_SendString(senha_correta);
				}
				else{
					USART_SendString("\r\Senha correta nao armazenou senha atualizada\r\n");
				}

				lcd_command(0x01);
				lcd_string("Senha salva");
				_delay_ms(1000);
				USART_SendString("\r\n Senha Salva com Sucesso!!: \r\n");

				modo_cadastro = 0;
				estado = SENHA_ANTERIOR;
				USART_SendString("\r\nDigite Senha: \r\n");

				lcd_command(0x01);
				lcd_string("Senha:");
				lcd_command(0xC0);
				break;
		}

		buf = NULL;
	}
}

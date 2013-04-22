/*
 * scales.c
 *
 * Created: 17/04/2013 20:47:06
 *  Author: Vlad Mesco
 */ 


#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

#define ST_INPUT 0
#define ST_WEIGHING 1
#define ST_RESET 0x80
#define ST_SLEEP 0xFE // ends in 0 in order to toggle to WEIGHING
                      //   if this button is pressed while sleeping

#define NUMBER_MAX 30
#define NUMBER_MIN 5

unsigned char state;
unsigned char number;

#define DEFINE_PRINT_FUNC(WHICH, PORT) \
void print##WHICH(unsigned int x) \
{ \
	switch(x) { \
	case 0: \
		PORT = (PORT & 0x80) | (0x7F & ~0xBF); \
		break; \
	case 1: \
		PORT = (PORT & 0x80) | (0x7F & ~0x86); \
		break; \
	case 2: \
		PORT = (PORT & 0x80) | (0x7F & ~0xDB); \
		break; \
	case 3: \
		PORT = (PORT & 0x80) | (0x7F & ~0xCF); \
		break; \
	case 4: \
		PORT = (PORT & 0x80) | (0x7F & ~0xE6); \
		break; \
	case 5: \
		PORT = (PORT & 0x80) | (0x7F & ~0xED); \
		break; \
	case 6: \
		PORT = (PORT & 0x80) | (0x7F & ~0xFC); \
		break; \
	case 7: \
		PORT = (PORT & 0x80) | (0x7F & ~0x87); \
		break; \
	case 8: \
		PORT = (PORT & 0x80) | (0x7F & 0x0); \
		break; \
	case 9: \
		PORT = (PORT & 0x80) | (0x7F & 0x10); \
		break; \
	case 'E': \
		PORT = (PORT & 0x80) | (0x7F & 0x6); \
		break; \
	case 'r': \
		PORT = (PORT & 0x80) | (0x7F & ~0x50); \
		break; \
	default: \
		PORT = (PORT & 0x80) | (0x7F & 0x6); \
	} \
}

ISR(INT2_vect, ISR_BLOCK)
{
	cli();
	if(GIFR & (1 << INTF2)) {
		if(state == ST_SLEEP) {
			reset();
		}			
		buttons();
		GIFR &= ~(1 << INTF2);
	}
	sei();
}

unsigned int readAnalog()
{
	// Start conversion by setting ADSC in ADCSRA Register
	ADCSRA |= (1<<ADSC);
	// wait until conversion complete ADSC=0 -> Complete
	while (ADCSRA & (1<<ADSC));
	// Get ADC the Result
	return ADCW;
}

DEFINE_PRINT_FUNC(Lo, PORTD)
DEFINE_PRINT_FUNC(Hi, PORTC)

// print a double-digit number
void output(unsigned int x)
{
#ifdef DEBUG_PRINT_STATE
	switch(state) {
		case ST_INPUT:
			printHi(0);
			printLo(0);
			break;
		case ST_WEIGHING:
			printHi(0);
			printLo(1);
			break;
		case ST_SLEEP:
			printHi(9);
			printLo(9);
			break;
		case ST_RESET:
			printHi(1);
			printLo(0);
			break;
		default:
			printHi('E');
			printLo('r');
	}
	return;
#else
	if(x < 100) {
		unsigned int hi = x / 10;
		unsigned int lo = x % 10;
	
		printHi(hi);
		printLo(lo);
	} else {
		printHi('E');
		printLo('r');
}
#endif
}

// handle buttons
void buttons() 
{
	if(PINB & (1 << PINB4)) {
		state = ST_INPUT;
		number = (number + 1) % (NUMBER_MAX - NUMBER_MIN + 1);
	} else if(PINB & (1 << PINB5)) {
		state = ST_INPUT;
		number = (NUMBER_MAX - NUMBER_MIN + 1 + number - 1) % (NUMBER_MAX - NUMBER_MIN + 1);
	} else if(PINB & (1 << PINB7)) {
		state = (state & 0x1) ^ 0x1;
	} else if(PINB & (1 << PINB6)) {
		state = ST_SLEEP;
	}
}

void reset()
{
	sei(); // set global interrupt enable
	
	DDRB = 0x00; // inputs
	PORTB = 0xFF; // activate pull-up
	
	// port c & d are output
	DDRC = 0xFF; // most significant bit is weighing mode
	PORTC = 0x7F;
	DDRD = 0xFF; // most significant bit is overweight led output
	PORTD = 0x7F;
	
	// only set ADEN when using it, otherwise it uses too much power
	// set up analog input
	// ADSP0:2 set division factor from 1 to 128
	// Set ADCSRA Register in ATMega168
	// ADEN <- enable
	// ADIF <- free running mode
	ADCSRA = (1 << ADEN) | (1 << ADIF) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
	// Set ADMUX Register in ATMega168
	// REFS0 <- make vcc reference
	// ADLAR <- left shift ADCW which is 10bit
	// lower 4 bits select positive input pin ; 0000 = ADC0 which is what I want
	ADMUX = (1 << REFS0);
	//ADMUX |= 0x10; // no gain
	
	MCUCSR = (0 << ISC2); // falling edge on INT2
	GICR = (1 << INT2); // activate interrupt on INT2
}

// PORTA is analog input
// PORTB is button inputs
// PORTC, PORTD for LCDs
int main(void)
{
reset:
	reset();

	state = ST_INPUT;
	number = 23 - NUMBER_MIN;
	
	while(1)
	{
		switch(state) {
		case ST_INPUT:
			PORTC &= 0x7F;
			PORTD &= 0x7F;
			output(number + NUMBER_MIN);
			// sleep and wait for interrupt again
			set_sleep_mode(SLEEP_MODE_PWR_DOWN);
			sleep_mode();
			break;
		case ST_WEIGHING: {
			// (r + 5) / 10 <- rounding, gain was x10 so remove that
			// divide by 2 because it inputs 5V @50kg
			unsigned short readValue = (readAnalog() + 5) / 20;
			output(readValue);
			if(readValue > number + NUMBER_MIN) {
				PORTD |= 0x80;
			} else {
				PORTD &= 0x7F;
			}
			PORTC |= 0x80;
			break; }
		case ST_SLEEP:
			PORTC = 0x7F;
			PORTD = 0x7F;
			ADCSRA = 0x0; // turn off ADC
			set_sleep_mode(SLEEP_MODE_PWR_DOWN);
			sleep_mode();
			break;
		case ST_RESET:
		default:
			goto reset;
		}
	}
}
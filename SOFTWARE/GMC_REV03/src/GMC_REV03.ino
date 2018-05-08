#include <EEPROMex.h>
#include <Arduino.h>
#include <EEPROMVar.h>
#include <elapsedMillis.h>
#include <avr/wdt.h>
#include <LiquidCrystal.h>
#include <SimpleModbusSlave.h>

//************ Define o pino em que a leitura de tensao e feito
#define HVTP A3
#define DBL 11

//************ Define o ponto de teste de alta tensao
float HVTPOINT;

//************ Define as conexoes do LCD
LiquidCrystal lcd(5, 6, 7, 8, 9, 10);

//************ Cria o caracter Micro
byte micro[8] = {
	0b00000,
	0b10001,
	0b10001,
	0b10011,
	0b11101,
	0b10000,
	0b10000,
	0b10000
};

//************ Variaveis responsaveis pelo tempo de execucao
elapsedMillis pMillis;
int tempo     = 0;
unsigned int interval  = 1000;

//************ Variaveis responsaveis pela media movel das contagens
unsigned int CPMLM  = 0;
unsigned long oCPMLM  = 0;

//************ Fator de conversao especifico para o tubo SBM-20m
const float convFact = 0.5747126; //--------------> Tube datasheet
//************ Variavel para armazenar o valor em microroentgen por hora
float microroentgen_hour = 0.0;

//----------------------Simple Moving Average------------------------
//************ Vetor com sessenta posicoes para armazenar as sessenta contagens de um minuto
int SA[60];
int aSA     = 0;
int sample  = 0;
long AVGA    = 0;

//************ Biblioteca modbus de terceiro

// ------------------- MODBUS --------------
/* This example code has 9 holding registers. 6 analogue inputs, 1 button, 1 digital output
   and 1 register to indicate errors encountered since started.
   Function 5 (write single coil) is not implemented so I'm using a whole register
   and function 16 to set the onboard Led on the Atmega328P.

   The modbus_update() method updates the holdingRegs register array and checks communication.

   Note:
   The Arduino serial ring buffer is 128 bytes or 64 registers.
   Most of the time you will connect the arduino to a master via serial
   using a MAX485 or similar.

   In a function 3 request the master will attempt to read from your
   slave and since 5 bytes is already used for ID, FUNCTION, NO OF BYTES
   and two BYTES CRC the master can only request 122 bytes or 61 registers.

   In a function 16 request the master will attempt to write to your
   slave and since a 9 bytes is already used for ID, FUNCTION, ADDRESS,
   NO OF REGISTERS, NO OF BYTES and two BYTES CRC the master can only write
   118 bytes or 59 registers.

   Using the FTDI USB to Serial converter the maximum bytes you can send is limited
   to its internal buffer which is 60 bytes or 30 unsigned int registers.

   Thus:

   In a function 3 request the master will attempt to read from your
   slave and since 5 bytes is already used for ID, FUNCTION, NO OF BYTES
   and two BYTES CRC the master can only request 54 bytes or 27 registers.

   In a function 16 request the master will attempt to write to your
   slave and since a 9 bytes is already used for ID, FUNCTION, ADDRESS,
   NO OF REGISTERS, NO OF BYTES and two BYTES CRC the master can only write
   50 bytes or 25 registers.

   Since it is assumed that you will mostly use the Arduino to connect to a
   master without using a USB to Serial converter the internal buffer is set
   the same as the Arduino Serial ring buffer which is 128 bytes.
*/


// Using the enum instruction allows for an easy method for adding and
// removing registers. Doing it this way saves you #defining the size
// of your slaves register array each time you want to add more registers
// and at a glimpse informs you of your slaves register layout.

//////////////// registers of your slave ///////////////////
enum
{
  // just add or remove registers and your good to go...
  // The first register starts at address 0
  ADDR0,
  ADDR1,

  TOTAL_ERRORS,
  // leave this one
  TOTAL_REGS_SIZE
  // total number of registers for function 3 and 16 share the same register array
};

unsigned int holdingRegs[TOTAL_REGS_SIZE]; // function 3 and 16 register array


void setup()
{
	//--------------- MODBUS Slave ---------------
	/* parameters(long baudrate,
								unsigned char ID,
								unsigned char transmit enable pin,
								unsigned int holding registers size,
								unsigned char low latency)

		 The transmit enable pin is used in half duplex communication to activate a MAX485 or similar
		 to deactivate this mode use any value < 2 because 0 & 1 is reserved for Rx & Tx.
		 Low latency delays makes the implementation non-standard
		 but practically it works with all major modbus master implementations.
	*/

modbus_configure(9600, 10, 3, TOTAL_REGS_SIZE, 0);

pinMode(DBL, OUTPUT);
digitalWrite(DBL, HIGH);

//************ Habilita as interrupcoes
attachInterrupt(0, incpulso, RISING);

//************ Cria o caracter micro
lcd.createChar(0, micro);

//************ Inicia o lcd
lcd.begin(16, 2);

//************ Define a posicao do cursor e escreve as informacoes de boas vindas
lcd.setCursor(4, 0);
lcd.print("CONTADOR");
lcd.setCursor(1, 1);
lcd.print("GEIGER-MULLER");
delay(3000);

lcd.clear();

lcd.setCursor(0, 0);
lcd.print("Hardware: VER04");
lcd.setCursor(0, 1);
lcd.print("Software: VER03");
delay(2000);

lcd.clear();
//************ Zera o timer
pMillis = 0;

}

void loop()
{
	//************ Chama a funcao CPM em cada volta do codigo
	CPM();
	//************ Le a tensao no pino de alta tensao
	analogRead(HVTP);

	//************ Converte a tensao com base no divisor de tensao de 100M
	HVTPOINT = analogRead(HVTP) * 5221.271885 / 1024.0;
	//************ Verifica se o jumper de alta tensao esta em uso, isto e feito analisando o valor do sinal
	//************ caso nao esteja em uso e feita a escrita dos valores de CPM na tela
	if(HVTPOINT < 100.0)
	{
		lcd.setCursor(0, 0);
		lcd.print("CPM:");
		lcd.setCursor(6, 0);
		lcd.print(oCPMLM);

		lcd.setCursor(0, 1);
		lcd.write(byte(0));
		lcd.setCursor(1, 1);
		lcd.print("R/h:");
		lcd.setCursor(6, 1);
		lcd.print(microroentgen_hour, 2);
	}
	//************ Caso o jumper esteja posicionado para ajuste da fonte, o valor da tensao e impresso na tela
	else {
		lcd.setCursor(0, 0);
		lcd.print(" TENSAO GERADOR");
		lcd.setCursor(13, 1);
		lcd.print("[V]");
		lcd.setCursor(4, 1);
		lcd.print(HVTPOINT, 1);
	}
}

void CPM() //----Funcao responsavel por realizar a conta de pulsos por minuto
{
  if (interval - pMillis <= 50)
  {
    tempo       = interval - pMillis;
    delay(tempo);
    cli();           //----------------  Desabilita a interrupcao
    oCPMLM = SMACPM(CPMLM);  //---------------- old Count Per Minute Last Minute equals CPMLM
    CPMLM = 0;       //---------------- Restart the counter
    pMillis = 0;     //----------------  Zera o timer da interrupcao
    sei();           //----------------  Habilita a interrupcao
		microroentgen_hour = convFact * oCPMLM; //-------------------- Convert CPM to mR/h
		lcd.clear();
		mbus();
    }
}

void incpulso () //------- Incrementa os pulso -------
{
  CPMLM++;
}

//************Media movel das interrupcoes em um minuto
long SMACPM(long CPS)
{
  AVGA    = 0;
  sample  = 0;
  SA[aSA] = CPS;
  for (sample = 0; sample <= 58; sample++)
  {
    AVGA += SA[sample];
  }
  if (aSA <= 58)
  {
    aSA++;
  } else
  {
    aSA = 0;
  }
  return AVGA;
}

//************ Funcao responsavel pelos dados enviados pelo protocolo modbus
void mbus()
{
  holdingRegs[TOTAL_ERRORS] = modbus_update(holdingRegs);

  holdingRegs[ADDR0] = oCPMLM;
  holdingRegs[ADDR1] = HVTPOINT;
}

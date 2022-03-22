#include "cowpi.h"
// Monty McConnell & Jaron David Nallathambi
struct gpio_registers *gpio;
struct spi_registers *spi;
struct timer_registers_16bit *timer1;

const uint8_t keys[4][4] = {
  {0x1, 0x2, 0x3, 0xA},
  {0x4, 0x5, 0x6, 0xB},
  {0x7, 0x8, 0x9, 0xC},
  {0xF, 0x0, 0xE, 0xD}
};

const uint8_t seven_segments[17] = {
  0b01111110, 0b00110000, 0b01101101, 0b01111001, // 0, 1, 2, 3
  0b00110011, 0b01011011, 0b01011111, 0b01110000, // 4, 5, 6, 7
  0b01111111, 0b01110011, 0b01110111, 0b00011111, // 8, 9, A, B
  0b00001101, 0b00111101, 0b01001111, 0b01000111, // C, D, E, F
  0b00000001                                      // -
  
};

const uint8_t err[8] = {
  0x00,0x00,0x00, 0b01001111, 0b00000101,
  0b00000101, 0b00011101, 0b00000101
};


volatile uint8_t key_pressed;
volatile unsigned int clock = 0;
volatile unsigned long last_keypad_press = 0;
volatile unsigned long last_left_button_press = 0;
volatile unsigned long last_right_button_press = 0;
unsigned long last_left_switch_slide = 0;
bool pressed_equals;
char op;
long number;
long operand1,operand2;
volatile bool isNegative = false;
volatile bool negatedNum = false;


unsigned long now;
bool screenOn = true;
bool showError = false;
volatile bool keyBeenPressed = false;
volatile uint8_t last_key_pressed;



void setup() {
  Serial.begin(9600);
  gpio = (struct gpio_registers *)(IObase + 0x03);
  spi = (struct spi_registers *)(IObase + 0x2C);
  timer1 = (struct timer_registers_16bit *)(IObase + 0x60);
  TCCR1A |= 0;
  TCCR1B = (1<<WGM12) | (1<<CS11) | (1<<CS10);

  TCNT1 = 0;
//  OCR1A = 250; // 16Mhz * 0.001 (1 ms) / 64 = 250 for 1ms and then have interrupt +1 to a counter every ms
//  OCR1A = 1250; // 16Mhz * 0.005 (5 ms) / 64 = 1250
  OCR1A = 31250; // = 16Mhz * 0.5 (500 ms) / 256
  TIMSK1 |= (1<<OCIE1A);
  setup_simple_io();
  setup_keypad();
  setup_display_module();
  pinMode(2,INPUT);
  pinMode(3,INPUT);  
  attachInterrupt ( digitalPinToInterrupt (2) , handle_buttonpress , CHANGE );
  attachInterrupt ( digitalPinToInterrupt (3) , handle_keypress , CHANGE );
}

void setup_simple_io() {
  gpio[A0_A5].direction &= 0b11001111;
  gpio[A0_A5].output &= 0b11001111;
  gpio[D8_D13].direction &= 0b11111100;
  gpio[D8_D13].output |= 0b00000011;
  gpio[D8_D13].direction |= 0b00010000;
}

void setup_keypad() {
  gpio[D0_D7].direction |= 0b11110000;
  gpio[D0_D7].output &= 0b00001111; 
  gpio[A0_A5].output |= 0b00001111;
}

void setup_display_module() {
  gpio[D8_D13].direction |= 0b00101100;
  spi->control = 0b01010001;
  for (char i = 1; i <= 8; i++) {
    display_data(i, 0);     // clear all digit registers
  }
  display_data(0xA, 8);     // intensity at 17/32
  display_data(0xB, 7);     // scan all eight digits
  display_data(0xC, 1);     // take display out of shutdown mode
  display_data(0xF, 0);     // take display out of test mode, just in case
}

void display_data(uint8_t address, uint8_t value) {
  // address is MAX7219's register address (1-8 for digits; otherwise see MAX7219 datasheet Table 2)
  // value is the bit pattern to place in the register
  gpio[D8_D13].output &= 0b11111011;
  spi->data = address;
  
  while((spi->status & 0b10000000) == 0b00000000){
  }
  
  spi->data = value;
  while((spi->status & 0b10000000) == 0b00000000){
  }
  gpio[D8_D13].output |= 0b00000100;
  
}

ISR(TIMER1_COMPA_vect){
  clock++;
}

void handle_buttonpress(){
  now = millis(); // for debouncing
  if (!digitalRead(8) && (now - last_left_button_press > 200)) {
    last_left_button_press = now;
    if(!screenOn){
      screenOn = true;
      refreshDisplay();
    }
    else{
      negatedNum = true;
    }
    clock = 0;
  }
  if (!digitalRead(9) && (now - last_right_button_press > 200)) {
    last_right_button_press = now;
    if(!screenOn){
      screenOn = true;
      refreshDisplay();
    }
    else{
      resetCalc();
    }
    clock = 0;
  }
}

volatile unsigned long lastDebounceTime = 0;
volatile int keyState;
volatile int lastKeyState;

void handle_keypress() {
  now = millis();
  if(now - lastDebounceTime > 300){
    lastDebounceTime = now;
    for(int i=0; i<4; i++){
        gpio[D0_D7].output |= 0b11110000;
        gpio[D0_D7].output &= ~(1 << (i + 4)); 

        if((gpio[A0_A5].input & 0b00001111) != 0b00001111){
          switch(gpio[A0_A5].input & 0b00001111) {
            case 0b00001110:
              key_pressed = keys[i][0];
              break;

            case  0b00001101:
              key_pressed = keys[i][1];
              break;

            case 0b00001011:
              key_pressed = keys[i][2];
              break;

            case 0b00000111:
              key_pressed = keys[i][3];
              break;
          }
        }
      }
      gpio[D0_D7].output &= 0b00001111;
      clock = 0;      
      keyBeenPressed = true;  
  }
  else{
    ; // do nothing
  }
}

void resetCalc(){
  clearDisplay();
  display_data(1,seven_segments[0]);
  number = operand1 = operand2 = 0;
}

bool startUp = true;
void loop(){
  if(startUp){
    display_data(1,seven_segments[0]);
  }
  uint8_t left_switch_current_position = gpio[A0_A5].input & (1 << 4);
  if(clock == 5000 && screenOn && (left_switch_current_position) ){
    startUp = false;
    clearDisplay();
    screenOn = false;
  }
  if(clock == 29500 && screenOn && !(left_switch_current_position) ){
    startUp = false;
    clearDisplay();
    screenOn = false;
  }
  if(negatedNum && number != 0){
        number *= -1;
        negatedNum = false;
        refreshDisplay();
  }
  if(keyBeenPressed){
    startUp = false;
    if(!screenOn){
      refreshDisplay();
      screenOn = true;
    }
    else {
      detectWhichKey();
    }    
    if (pressed_equals == true){
      findResult();
      pressed_equals = false;
    }
    
    keyBeenPressed = false;
  }
}


bool operatorPressed = false;
void detectWhichKey(){

  if(operatorPressed){
      number = 0;
      operatorPressed = false;
  }

  if (key_pressed < 10){
    if(number == 0) {
      number = key_pressed;
    }
    else {
      number = (number * 10) + key_pressed;
    }
    refreshDisplay();
  }

  if (key_pressed == 0xE) {
    operand2 = number;
    pressed_equals = true;
  }

  if(key_pressed == 0xA || key_pressed == 0xB || key_pressed == 0xC || key_pressed == 0xD){
    operand1 = number;
    operatorPressed = true;
    if (key_pressed == 0xA) {
       op = '+';
    }
    if (key_pressed == 0xB) {
       op = '-';
    }
    if (key_pressed == 0xC) {
       op = '*';
    }
    if (key_pressed == 0xD) {
       op = '/';
    }
    else {
      refreshDisplay();
    }
  }
}

void clearDisplay(){
  for(int i = 1; i <= 8; i++){
    display_data(i,0);
  }
}

void printError() {
  for(int i = 1; i <= 8; i++){
    display_data(i,err[8-i]);
  }
}

void findResult(){
  if (op == '+') {
    number = operand1 + operand2;
  }
  if (op == '-') {
    number = operand1 - operand2;
  }
  if (op == '*') {
    number = operand1 * operand2;
  }
  if (op == '/') {
    if(operand2 == 0) {
      number = 0;
      operand1 = 0;
      operand2 = 0;
      printError();
      return;
    } 
    else{
      number = operand1 / operand2;
    }
  }
  
  if (number < 0) {
    isNegative = true;
  }

  refreshDisplay();
  operand1 = operand2 = 0;
}

void refreshDisplay() {
  
  String data = String(number);
  if (data.length() > 8) {
    if (pressed_equals == true) { // overflow
      printError();
      number = 0;
    }
    else {
      number = data.substring(0,8).toInt(); // convert back to int
    }
  }
  else {
    for(int i = 1; i <= 8; i++){
      display_data(i,0);
    }
    for(int i = 1; i <= data.length(); i++){
      if(number < 0){
        display_data(i,seven_segments[(data[data.length() - i] - '0')]); // convert char to int
        display_data(data.length(), seven_segments[16]);
      }
      else {
        display_data(i,seven_segments[(data[data.length() - i] - '0')]);
      } 
    }
  }
}

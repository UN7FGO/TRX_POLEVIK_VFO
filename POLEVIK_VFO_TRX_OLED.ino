// http://un7fgo.gengen.ru (C) 2019
// https://github.com/UN7FGO 
// 
// Базовый, 3-х диапазонный синтезатор частоты для трансивера ПОЛЕВИК.

// Подключаем библиотеки для нашего OLED дистплея, подключенного по I2C протоколу
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// Библиотека для ситезатора
#include "si5351.h"
// Библиотека для обработки энкодера
#include <RotaryEncoder.h>

// Описываем подключение дисплея по шине I2C
#define SCREEN_WIDTH  128 // OLED display width, in pixels
#define SCREEN_HEIGHT  64 // OLED display height, in pixels
#define OLED_RESET      4 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Создаем переменную-объект для синтезатора
Si5351 si5351;
 
// Определяем контакты, к которым у нас подключен энкодер
#define ENC_DT_PIN  9
#define ENC_CLK_PIN 8
#define ENC_SW_PIN  7
// Создаем переменную-объект для работы с энкодером
RotaryEncoder encoder(ENC_DT_PIN, ENC_CLK_PIN);   

// Определяем контакт, к которому подключен делитель входного напряжения 1:3
#define VOLT_PIN A0
// Определяем сопротивления резисторов в делителе напряжения питания
#define VOLT_R1 5700
#define VOLT_R2 2000

// Определяем контакт, к которому подключен потенциометр регулировки скорости электронного ключа
#define CW_SPEED_PIN A1
// Определяем максимальную и минимальную длительность точки в миллисекундах
#define MIN_DIT_TIME 50
#define MAX_DIT_TIME 200
// частота звука для контроля
#define CW_AUDIO_FREQUENCY 800

// Определяем контакты для подключения электронного ключа
#define CW_DIT_PIN 5
#define CW_DASH_PIN 6
// Контакт вывода звука для самоконтроля
#define CW_SOUND_PIN 13
// контакт "ключа".
#define CW_OUT_PIN 3

// Количество диапазонов и массивы с их параметрами 
#define MAXBAND 3
// массив "текущих частот" 
long int cur_freq[MAXBAND]  = {3600000, 7100000, 14100000};
// массив "максимальных частот" 
long int max_freq[MAXBAND]  = {3800000, 7300000, 14350000};
// массив "минимальных частот" 
long int min_freq[MAXBAND]  = {3500000, 7000000, 14000000};
// массив "выводов для перключения ДПФ" 
long int lbp_pin[MAXBAND]  = {10, 11, 12};
// максимальное количество шагов перестройки частоты
#define MAXFREQ 4
// массив "шагов перестройки" в Герцах
long int d_freq[MAXFREQ]  = { 1000, 100, 10, 1};

// переменные для работы
unsigned long int k, fr, current_freq;
unsigned long int old_freq;
unsigned long int pressed;
int Band, t_delay, nfreq, dfreq, Pos;
float KOEF_V;
/* =================================================== */
void setup() {
  Serial.begin(9600);
 
  // определяем режимы работы цифровых входов
  pinMode (ENC_CLK_PIN,INPUT_PULLUP);
  pinMode (ENC_DT_PIN,INPUT_PULLUP);
  pinMode (ENC_SW_PIN,INPUT_PULLUP);
  pinMode (CW_DIT_PIN,INPUT_PULLUP);
  pinMode (CW_DASH_PIN,INPUT_PULLUP);
  digitalWrite(CW_OUT_PIN, LOW);
  // "устанавливаем" энкодер
  encoder.setPosition(0);
  
  // расчитываем коэфициент для делителя напряжения питания
  KOEF_V = ( 50 * ( VOLT_R1 + VOLT_R2 ) ) / (1023 * VOLT_R2 );
  
  // "старая" частота "по умолчанию"
  old_freq = 0;
  // Текужий диапазон
  Band = 2;
  // Текущая частота
  current_freq = cur_freq[Band];
  // Текущий номер шага изменения частоты
  nfreq = 2;
  // Текущий шаг изменения частоты
  dfreq = d_freq[nfreq];
  // Переключаем входной ДПФ приемника
  for (int i=0; i<MAXBAND; i++){ digitalWrite(lbp_pin[i], LOW); } 
  digitalWrite(lbp_pin[Band], HIGH);
      
  // Инициализируем наш дисплей
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println("SSD1306 allocation failed");
  }
  
  // Инициализируем синтезатор
  bool i2c_found;
  i2c_found = si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
  Serial.println(i2c_found);
  if(!i2c_found) { Serial.println("SSI5351 allocation failed"); }

  // выставляем на выходе 1-го генератора асинтезатора максимальный уровень сигнала
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);  
  // Обновляем информацию на дисплее
  Refresh_Display();
}

void loop() {
  
  // Если частота у нас изменилась, 
  // то обновляем ее значение на индикаторе и на синтезаторе
  if ( current_freq != old_freq ) {
    si5351.set_freq(current_freq*100, SI5351_CLK0); 
    old_freq = current_freq;
    Refresh_Display();
  }

  // обрабатываем кнопку энкодера
  if (digitalRead(ENC_SW_PIN) == 0) {
    // запомнаем время нажатия кнопки
    pressed = millis();
    // ждем, пока кнопку отпустят
    while (digitalRead(ENC_SW_PIN) == 0) {
    }
    // считаем время, сколько была нажата кнопка
    pressed = millis() - pressed;
    // если время нажатия больше 1 секунды, то переключаем диапазон
    if ( pressed > 1000 ) {
      // запоминаем текущую частоту на текущем диапазоне
      cur_freq[Band] = current_freq;
      // увеличиваем номер диапазона
      Band +=1;
      // если номер больше максимального, возвращаемся в начало
      if ( Band == MAXBAND ) {
        Band = 0;
      }
      // считываем текущую частоту выбранного диапазона
      current_freq = cur_freq[Band];
       // Переключаем входной ДПФ приемника
      for (int i=0; i<MAXBAND; i++){
       digitalWrite(lbp_pin[i], LOW);
      } 
      digitalWrite(lbp_pin[Band], HIGH);     
      // Обновляем информацию на дисплее      
      Refresh_Display();      
    } else {
      // если кнопка былв нажаты менее 1 секунды, меняем шаг перестройки
      // переходим на следующий шаг
      nfreq += 1;
      // если шаг больше возможного, переходим к первому значению
      if ( nfreq == MAXFREQ ) {
        nfreq = 0;
      }
      // запоминаем выбранный шаг перестройки
      dfreq = d_freq[nfreq];
      // выводим на индикатор информацию о выбранном шаге перестройки
      Refresh_Display();
    }
  }

  // обрабатываем энкодер
  encoder.tick();
  Pos = encoder.getPosition();
  // проверяем, был ли произведен поворот ручки энкодера
  if (Pos != 0){ 
    // определяем направление вращения энкодера
    if (Pos < 0) {
       // повернули энкодер "по часовой стрелке" (CW)
       current_freq += dfreq;
       // не даем частоте уйти за верхний предел диапазона
       if ( current_freq > max_freq[Band] ) {
         current_freq = max_freq[Band];
       }
     } else {
       // повернули энкодер "против часовой стрелки" (CCW)
       current_freq -= dfreq;
       // не даем частоте уйти за нижний предел диапазона
       if ( current_freq < min_freq[Band] ) {
         current_freq = min_freq[Band];
       }
     }
     encoder.setPosition(0);
  }

// обрабатываем нажатие манипулятора "точка"
  if(digitalRead(CW_DIT_PIN) == 0) {
    // расчитываем длительность "точки" по положению потенциометра
    t_delay = analogRead(CW_SPEED_PIN)*(MAX_DIT_TIME - MIN_DIT_TIME)/1023 + MIN_DIT_TIME;
    // нажимаем "ключ"      
    digitalWrite(CW_OUT_PIN, HIGH);
    // включаем звуковой тон
    tone(CW_SOUND_PIN, CW_AUDIO_FREQUENCY);
    // задержка длительности звука/нажатия ключа      
    delay(t_delay);
    // выключаем звуковой тон      
    noTone(CW_SOUND_PIN);
    // "отпускаем" ключ
    digitalWrite(CW_OUT_PIN, LOW);
    // выдерживаем паузу между точками/тире      
    delay(t_delay);
  }

//   обрабатываем нажатие манипулятора "тире"
  if(digitalRead(CW_DASH_PIN) == 0) {
    // расчитываем длительность "точки" по положению потенциометра
    t_delay = analogRead(CW_SPEED_PIN)*(MAX_DIT_TIME - MIN_DIT_TIME)/1023 + MIN_DIT_TIME;
    // нажимаем "ключ"      
    digitalWrite(CW_OUT_PIN, HIGH);
    // включаем звуковой тон
    tone(CW_SOUND_PIN, CW_AUDIO_FREQUENCY);
    // задержка длительности звука/нажатия ключа      
    // так как у нас тут "тире", то длительность его в 3 раза больше, чем для точки
    delay(t_delay*3);
    // выключаем звуковой тон      
    noTone(CW_SOUND_PIN);
    // "отпускаем" ключ
    digitalWrite(CW_OUT_PIN, LOW);
    // выдерживаем паузу между точками/тире      
    delay(t_delay);
  }
// конец основного цикла
}

// функция для расчета степени числа 10
long int intpow(int p) {
  long int k = 1;
  for (int j=1; j<p; j++) {
    k = k * 10;
  }
  return k;
}

// Процедура вывода информации на дисплей
void Refresh_Display()
{
  int ost,yy;
  String S, Ss;
  // задаем настройки отображения дисплея
  display.clearDisplay();
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
  display.setTextSize(2);      
  display.setTextColor(SSD1306_WHITE);
  // выводим текущую частоту в формате ##.###.###
  display.setCursor(0, 0);
    Ss = ""; 
    fr = current_freq;
    for (int i=8; i>0; i--) {
      k = intpow(i);
      ost = fr / k;
      Ss += ost;
      fr = fr % k; 
      if (i == 7 || i == 4) {
        Ss += ".";    
      }
    }
    for (int i=0; i<=9; i++) {
      display.print(Ss[i]);
    }
  // выводим на экран текуший шаг изменеия частоты  
  display.setCursor(0, 24); 
  display.print(F("Step: "));
  display.print(dfreq);
  // выводим на экран напряжение питания
  display.setCursor(0, 48);
  display.print(F("VCC "));
  display.print(float(int(analogRead(VOLT_PIN)*KOEF_V)/10) );
  display.print(F("v"));
  // обновляем дисплей
  display.display();
}

#include <Adafruit_MCP23017.h>
#include <Adafruit_RGBLCDShield.h>
#include <Wire.h>
#include <EEPROM.h>
#define FREQ 490
#define INT_PIN 0
#define CLOCK_OUT_PIN 10
#define LED_PIN 13
#define MAX_EVENTS 20
#define ID_BYTE_0       0x47
#define ID_BYTE_1       0xf5
#define SAFE            0xf1

#define DISPLAY_TIMEOUT 30

#define LCD_MODE_INIT   0
#define LCD_MODE_TIME   1
#define LCD_MODE_SET_TIME 2

#define RED             0x1
#define YELLOW          0x3
#define GREEN           0x2
#define TEAL            0x6
#define BLUE            0x4
#define VIOLET          0x5
#define WHITE           0x7


#define BLOCK_SIZE      8

#define H_ID_BYTE_0     0
#define H_ID_BYTE_1     1
#define H_EVENT_COUNT   2
#define H_FIRST_FREE    3
#define H_LAST_USED     4
#define H_SAFE          5
#define H_EVENT_PRESENT 6


#define O_EVENT_PRESENT 0
#define O_WKDAYS        1
#define O_SBYTE0        2
#define O_SBYTE1        3
#define O_SBYTE2        4
#define O_SBYTE3        5
#define O_PIN_STATE     6

/*==============================================================================*/
/* Useful Constants */
#define SECS_PER_MIN  (60UL)
#define SECS_PER_HOUR (3600UL)
#define SECS_PER_DAY  (SECS_PER_HOUR * 24UL)
#define DAYS_PER_WEEK (7UL)
#define SECS_PER_WEEK (SECS_PER_DAY * DAYS_PER_WEEK)
#define SECS_PER_YEAR (SECS_PER_WEEK * 52UL)
#define SECS_YR_2000  (946684800UL) // the time at the start of y2k
 
/* Useful Macros for getting elapsed time */
#define numberOfSeconds(_time_) (_time_ % SECS_PER_MIN)  
#define numberOfMinutes(_time_) ((_time_ / SECS_PER_MIN) % SECS_PER_MIN) 
#define numberOfHours(_time_) (( _time_% SECS_PER_DAY) / SECS_PER_HOUR)
#define dayOfWeek(_time_)  ((( _time_ / SECS_PER_DAY + 4)  % DAYS_PER_WEEK)+1) // 1 = Sunday
#define elapsedDays(_time_) ( _time_ / SECS_PER_DAY)  // this is number of days since Jan 1 1970
#define elapsedSecsToday(_time_)  (_time_ % SECS_PER_DAY)   // the number of seconds since last midnight 
// The following macros are used in calculating alarms and assume the clock is set to a date later than Jan 1 1971
// Always set the correct time before settting alarms
#define previousMidnight(_time_) (( _time_ / SECS_PER_DAY) * SECS_PER_DAY)  // time at the start of the given day
#define nextMidnight(_time_) ( previousMidnight(_time_)  + SECS_PER_DAY )   // time at the end of the given day 
#define elapsedSecsThisWeek(_time_)  (elapsedSecsToday(_time_) +  ((dayOfWeek(_time_)-1) * SECS_PER_DAY) )   // note that week starts on day 1
#define previousSunday(_time_)  (_time_ - elapsedSecsThisWeek(_time_))      // time at the start of the week for the given time
#define nextSunday(_time_) ( previousSunday(_time_)+SECS_PER_WEEK)          // time at the end of the week for the given time


/* Useful Macros for converting elapsed time to a time_t */
#define minutesToTime_t ((M)) ( (M) * SECS_PER_MIN)  
#define hoursToTime_t   ((H)) ( (H) * SECS_PER_HOUR)  
#define daysToTime_t    ((D)) ( (D) * SECS_PER_DAY) // fixed on Jul 22 2011
#define weeksToTime_t   ((W)) ( (W) * SECS_PER_WEEK)   
#define weekDayMask(_day_) ( (byte)(1 << _day_ - 1 ))
namespace Cron {
  typedef struct {
    byte second;
    byte minute;
    byte hour;
    byte wday;
    byte day;
    byte month;
    byte year;
  } Time;

  typedef struct {
    byte state;
    byte wdays;
    bool enable;
    byte blkAdr;
    unsigned long stamp;
  } Event;

  #define LEAP_YEAR(Y)     ( ((1970+Y)>0) && !((1970+Y)%4) && ( ((1970+Y)%100) || !((1970+Y)%400) ) )

  static  const uint8_t monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31};
  Cron::Event events[MAX_EVENTS];
  Cron::Time now;
  unsigned long nextEventTime;
  int nextEventId;

  Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();
  int currentEventId;

  bool stateChanged;
  byte state;
  byte eventPresent;
  byte lcdMode;

  //header file inline


  void breakTime(unsigned long timeInput, Time &t);
  bool isToday(unsigned long timeInput, Event &e);
  int nextEventToday(unsigned long timeInput);
  int currentEventToday(unsigned long timeInput);
  int nextEvent(unsigned long timeInput);
  int currentEvent(unsigned long timeInput);
  int createEvent(byte wdays, byte hour, byte minute, byte second, byte pinState);
  void destroyEvent(int id);
  bool cronTick(unsigned long timeInput);
  void cronInitEvents();
  void writeEeoromHeader();
  int cronInit(unsigned long timeInput);
  bool actionRequired();
  void dateString(unsigned long timeInput, char* st);
  void lcdInit();

  void breakTime(unsigned long timeInput, Time &t) {
    uint8_t year;
    uint8_t month, monthLength;
    uint32_t time;
    unsigned long days;

    time = timeInput;
    t.second = time % 60;
    time /= 60; // now it is minutes
    t.minute = time % 60;
    time /= 60; // now it is hours
    t.hour = time % 24;
    time /= 24; // now it is days
    t.wday = ((time + 4) % 7) + 1;  // Sunday is day 1 

    year = 0;
    days = 0;
    while((unsigned)(days += (LEAP_YEAR(year) ? 366 : 365)) <= time) {
      year++;
    }
    t.year = year; // year is offset from 1970 

    days -= LEAP_YEAR(year) ? 366 : 365;
    time  -= days; // now it is days in this year, starting at 0

    days=0;
    month=0;
    monthLength=0;
    for (month=0; month<12; month++) {
      if (month==1) { // february
        if (LEAP_YEAR(year)) {
          monthLength=29;
        } else {
          monthLength=28;
        }
      } else {
        monthLength = monthDays[month];
      }

      if (time >= monthLength) {
        time -= monthLength;
      } else {
        break;
      }
    }
    t.month = month + 1;  // jan is month 1  
    t.day = time + 1;     // day of month
  }
  bool isToday(unsigned long timeInput, Event &e) {
    if( (weekDayMask(dayOfWeek(timeInput)) & e.wdays) > 0) {
      return true;
    } else {
      return false;
    }
  }
  int nextEventToday(unsigned long timeInput) {
    unsigned long currentClosestTime = nextMidnight(timeInput);
    int currentClosestID = -1;
    for(int i = 0; i < MAX_EVENTS; i++) {
      if(events[i].enable && isToday(timeInput, events[i])) {
        unsigned long today = previousMidnight(timeInput) + events[i].stamp;
        if(today > timeInput && (today - timeInput) < (currentClosestTime - timeInput)) {
          currentClosestTime = today;
          currentClosestID = i;
        }
      }
    }
    nextEventTime = currentClosestTime;
    return currentClosestID;
  }
  int currentEventToday(unsigned long timeInput) {
    unsigned long currentClosestTime = previousMidnight(timeInput);
    int currentClosestID = -1;
    for(int i = 0; i < MAX_EVENTS; i++) {
      if(events[i].enable && isToday(timeInput, events[i])) {
        unsigned long today = previousMidnight(timeInput) + events[i].stamp;
        if(today < timeInput && (timeInput - today) < (timeInput - currentClosestTime)) {
          currentClosestTime = today;
          currentClosestID = i;
        }
      }
    }
    return currentClosestID;
  }


  int nextEvent(unsigned long timeInput) {
    int id = nextEventToday(timeInput);
    if(id >= 0) {
      return id;
    } else {
      unsigned long time = nextMidnight(timeInput);
      for(int i = 0; i < 7; i++) {
        id = nextEventToday(time + (i*SECS_PER_DAY));
        if(id >= 0) return id;
      }
    }
    return -1;
  }
  int currentEvent(unsigned long timeInput) {
    int id = currentEventToday(timeInput);
    if(id >= 0) {
      return id;
    } else {
      unsigned long time = previousMidnight(timeInput) - 1;
      for(int i = 0; i < 7; i++) {
        id = currentEventToday(time - (i*SECS_PER_DAY));
        if(id >= 0) return id;
      }
    }
    return -1;
  }

  int createEvent(byte wdays, byte hour, byte minute, byte second, byte pinState) {
    int id = -1;
    byte blkAdr = EEPROM.read(H_FIRST_FREE);
    if(blkAdr == 255)
      return -2;
    for(int i = 0; i < MAX_EVENTS; i++) {
      if(!events[i].enable) {
        id = i;
        break;
      }
    }

    if(id < 0) return id;

    events[id].wdays = wdays;
    events[id].enable = true;
    events[id].state = pinState;
    events[id].stamp = (hour*SECS_PER_HOUR) + (minute*SECS_PER_MIN) + second;
    events[id].blkAdr = blkAdr;

    if(EEPROM.read(blkAdr*BLOCK_SIZE) != eventPresent) {
      EEPROM.write(H_SAFE, 0);
      byte c = EEPROM.read(H_EVENT_COUNT) + 1;
      Serial.println(c);
      EEPROM.write(H_EVENT_COUNT, c);
      EEPROM.write(blkAdr*BLOCK_SIZE + O_EVENT_PRESENT, eventPresent);
      EEPROM.write(blkAdr*BLOCK_SIZE + O_WKDAYS, wdays);
      EEPROM.write(blkAdr*BLOCK_SIZE + O_SBYTE0, (byte)events[id].stamp);
      EEPROM.write(blkAdr*BLOCK_SIZE + O_SBYTE1, (byte)(events[id].stamp >> 8));
      EEPROM.write(blkAdr*BLOCK_SIZE + O_SBYTE2, (byte)(events[id].stamp >> 16));
      EEPROM.write(blkAdr*BLOCK_SIZE + O_SBYTE3, (byte)(events[id].stamp >> 24));
      EEPROM.write(blkAdr*BLOCK_SIZE + O_PIN_STATE, pinState);
      if(blkAdr > EEPROM.read(H_LAST_USED))
        EEPROM.write(H_LAST_USED, blkAdr);
      for(int i = blkAdr + 1; i < MAX_EVENTS+1; i++) {
        if(EEPROM.read(i*BLOCK_SIZE) != eventPresent) {
          EEPROM.write(H_FIRST_FREE,i);
          break;
        }
        if(i == MAX_EVENTS && EEPROM.read(i*BLOCK_SIZE) != eventPresent)
          EEPROM.write(H_FIRST_FREE, 255);
      }
      EEPROM.write(H_SAFE, SAFE);
    }
    return id;

  }
  void destroyEvent(int id){
    if(id >= 0 && id < MAX_EVENTS) {
      events[id].enable = false;

      byte adr = events[id].blkAdr;
      if(adr !=255 && adr <= MAX_EVENTS) {
        EEPROM.write(H_SAFE, 0);
        EEPROM.write(adr * BLOCK_SIZE + O_EVENT_PRESENT, 255);
        if(adr < EEPROM.read(H_FIRST_FREE))
          EEPROM.write(H_FIRST_FREE, adr);
        if(adr == EEPROM.read(H_LAST_USED)) {
          for(int i = adr - 1; i >= 0; i--) {
            if(EEPROM.read(i*BLOCK_SIZE + O_EVENT_PRESENT) == eventPresent)
              EEPROM.write(H_LAST_USED, i);
            if(i == 0)
              EEPROM.write(H_LAST_USED, 0);
          }
        }
        events[id].blkAdr = 255;
        EEPROM.write(H_SAFE,SAFE);
      }
    }
  }

  bool cronTick(unsigned long timeInput) {
    if(timeInput >= nextEventTime) {
      stateChanged = true;
      state = events[nextEventId].state;
      currentEventId = currentEvent(timeInput + 1);
      nextEventId = nextEvent(timeInput);
      return true;
    }
    return false;

  }
  void cronInitEvents() {
    for(int i = 0; i < MAX_EVENTS;i++) {
      events[i].enable = false;
      events[i].blkAdr = 255;
    }
    byte b0 = EEPROM.read(0);
    byte b1 = EEPROM.read(1);
    if(b0 == ID_BYTE_0 && b1 == ID_BYTE_1) {
      if(EEPROM.read(5) == SAFE) {
        //Serial.println("header was safe");
        byte evRd = 0;
        byte evCnt = EEPROM.read(H_EVENT_COUNT);
        eventPresent = EEPROM.read(H_EVENT_PRESENT);
        for(int i = 1; i < MAX_EVENTS + 1; i++) {
          if(EEPROM.read(i*BLOCK_SIZE + O_EVENT_PRESENT) == eventPresent) {
            events[evRd].enable = true;
            events[evRd].wdays = EEPROM.read(i*BLOCK_SIZE + O_WKDAYS);
            events[evRd].state = EEPROM.read(i*BLOCK_SIZE + O_PIN_STATE);

            events[evRd].stamp = 0;
            events[evRd].stamp = (events[evRd].stamp | (unsigned long)EEPROM.read(i*BLOCK_SIZE + O_SBYTE3)) << 8;
            events[evRd].stamp = (events[evRd].stamp | (unsigned long)EEPROM.read(i*BLOCK_SIZE + O_SBYTE2)) << 8;
            events[evRd].stamp = (events[evRd].stamp | (unsigned long)EEPROM.read(i*BLOCK_SIZE + O_SBYTE1)) << 8;
            events[evRd].stamp = (events[evRd].stamp | (unsigned long)EEPROM.read(i*BLOCK_SIZE + O_SBYTE0));

            events[evRd].blkAdr = i;
            //Serial.print("timestamp: ");
            //Serial.println(events[evRd].stamp);
            evRd++;
          }
          if(evRd >= evCnt)
            break;
        }
      } else {
        writeEeoromHeader();
        //TODO panic();
      }
    } else{
      writeEeoromHeader();
    }
  }
  void writeEeoromHeader() {
    //HEADER
    //byte 0 = structure ID_BYTE_0
    //byte 1 = structure ID_BYTE_1
    //byte 2 = number of events
    //byte 3 = first free block
    //byte 4 = last used block
    //byte 5 = safe data (if unsafe clear data)
    //byte 6 = value indicating a valid stored event
    EEPROM.write(H_ID_BYTE_0, ID_BYTE_0);
    EEPROM.write(H_ID_BYTE_1, ID_BYTE_1);
    EEPROM.write(H_EVENT_COUNT, 0);
    EEPROM.write(H_FIRST_FREE, 1);
    EEPROM.write(H_LAST_USED, 0);
    EEPROM.write(H_SAFE, SAFE);
    eventPresent = (byte)micros();
    if(eventPresent == 255)
      eventPresent += 56;
    EEPROM.write(H_EVENT_PRESENT,eventPresent); //random enough to start with
  }
  int cronInit(unsigned long timeInput) {
    currentEventId = currentEvent(timeInput);
    nextEventId = nextEvent(timeInput);
    stateChanged = true;
    if(nextEventId >= 0) {
      state = events[currentEventId].state;
    }
    return currentEventId;
  }
  bool actionRequired() {
    if(stateChanged) {
      stateChanged = false;
      return true;
    }
    return false;
  }
  void lcdInit() {
    lcd.begin(16,2);
    lcdMode = LCD_MODE_INIT;
  }
  void lcdSetTime() {
    lcdMode = LCD_MODE_SET_TIME;
    lcd.setCursor(5,0);
    lcd.print('/');
    lcd.setCursor(8,0);
    lcd.print('/');
    lcd.setCursor(6,1);
    lcd.print(':');
    lcd.setCursor(9,1);
    lcd.print(':');
    byte pos = 0;
    Time tmp;
    tmp.second = 0;
    tmp.minute = 0;
    tmp.hour = 0;
    tmp.day = 1;
    tmp.month = 1;
    tmp.year = 30;
    while(true) {
    }

  }
  void lcdDisplayDate(unsigned long timeInput) {
    if(lcdMode != LCD_MODE_TIME) {
      lcd.setCursor(8,0);
      lcd.print('/');
      lcd.setCursor(11,0);
      lcd.print('/');
      lcd.setCursor(6,1);
      lcd.print(':');
      lcd.setCursor(9,1);
      lcd.print(':');
      lcdMode = LCD_MODE_TIME;
    }
    Time tmp;
    breakTime(timeInput, tmp);
    if(tmp.wday != now.wday) {
      lcd.setCursor(2,0);
      switch(tmp.wday) {
        case 1:
          lcd.print("SUN");
          break;
        case 2:
          lcd.print("MON");
          break;
        case 3:
          lcd.print("TUE");
          break;
        case 4:
          lcd.print("WED");
          break;
        case 5:
          lcd.print("THU");
          break;
        case 6:
          lcd.print("FRI");
          break;
        case 7:
          lcd.print("SAT");
          break;
        default:
          lcd.print("ERR");
      }
    }
    if(tmp.month != now.month) {
      lcd.setCursor(6,0);
      if(tmp.month < 10) lcd.print('0');
      lcd.print(tmp.month);
    }
    if(tmp.day != now.day) {
      lcd.setCursor(9,0);
      if(tmp.day < 10) lcd.print('0');
      lcd.print(tmp.day);
    }
    if(tmp.year != now.year) {
      lcd.setCursor(12,0);
      unsigned int year = (unsigned int)tmp.year + 1970;
      year -= (year / 1000) * 1000;
      year -= (year / 100) * 100;
      if(year < 10) lcd.print('0');
      lcd.print(year);
    }
    if(tmp.hour != now.hour) {
      lcd.setCursor(4,1);
      if(tmp.hour < 10) lcd.print('0');
      lcd.print(tmp.hour);
    }
    if(tmp.minute != now.minute) {
      lcd.setCursor(7,1);
      if(tmp.minute < 10) lcd.print('0');
      lcd.print(tmp.minute);
    }
    if(tmp.second != now.second) {
      lcd.setCursor(10,1);
      if(tmp.second < 10) lcd.print('0');
      lcd.print(tmp.second);
    }
    now = tmp;

  }
  void dateString(unsigned long timeInput, char* st) {
    breakTime(timeInput, now);
    if(now.month < 10) {
      st[0] = '0';
      st[1] = (now.month % 10) + 48;
    } else {
      st[0] = ((now.month / 10) % 10) + 48;
      st[1] = (now.month % 10) + 48;
    }
    st[2] = '/';
    if(now.day < 10) {
      st[3] = '0';
      st[4] = (now.day % 10) + 48;
    } else {
      st[3] = ((now.day / 10) % 10) + 48;
      st[4] = (now.day % 10) + 48;
    }
    st[5] = '/';
    int y = now.year + 1970;
    st[6] = (char)((y / 10) % 10) + 48;
    st[7] = (char)(y % 10) + 48;
    st[8] = ' ';
    if(now.hour < 10) {
      st[9] = '0';
      st[10] = (now.hour % 10) + 48;
    } else {
      st[9] = ((now.hour / 10) % 10) + 48;
      st[10] = (now.hour % 10) + 48;
    }
    st[11] = ':';
    if(now.minute < 10) {
      st[12] = '0';
      st[13] = (now.minute % 10) + 48;
    } else {
      st[12] = ((now.minute / 10) % 10) + 48;
      st[13] = (now.minute % 10) + 48;
    }
    st[14] = ':';
    if(now.second < 10) {
      st[15] = '0';
      st[16] = (now.second % 10) + 48;
    } else {
      st[15] = ((now.second / 10) % 10) + 48;
      st[16] = (now.second % 10) + 48;
    }
    st[17]=NULL;
  }
}


volatile unsigned int tick;
volatile unsigned long utime;
volatile bool update;
unsigned long previous;
char in_data[20];
char date[18];
byte timeout;

void setup() {
  tick = 1;
  utime = 1393262984;
  update = false;
  pinMode(LED_PIN, OUTPUT);
  pinMode(CLOCK_OUT_PIN, OUTPUT);
  pinMode(2, INPUT);
  analogWrite(CLOCK_OUT_PIN, 127);
  Serial.begin(57600);
  attachInterrupt(INT_PIN, onTick, RISING);
  Cron::cronInitEvents();
  //Cron::createEvent(weekDayMask(2),17,30,0,1);
  //Cron::createEvent(weekDayMask(2),17,30,15,2);
  //Cron::createEvent(weekDayMask(2),17,30,20,4);
  //Cron::createEvent(weekDayMask(2),17,30,25,8);
  //Cron::createEvent(weekDayMask(2),17,30,30,16);
  //Cron::createEvent(weekDayMask(2),17,30,35,32);
  //Cron::createEvent(weekDayMask(2) | weekDayMask(4),17,30,40,64);
  //Cron::createEvent(weekDayMask(2) | weekDayMask(4),17,30,45,64);
 // Cron::destroyEvent(3);
  Cron::cronInit(utime);
  Cron::lcdInit();
  Cron::lcd.cursor();
  Cron::lcd.blink();
  timeout = DISPLAY_TIMEOUT;
  previous = 0;
}

void onTick() {
  if(tick == FREQ) {
    update = true;
    tick = 0;
    utime++;
  }
  tick++;
  return;
}


void loop() {
  if(update) {
    update = false;
    //unsigned long prev = millis();
    //Cron::breakTime(utime, Cron::now);
    if(timeout > 0) {
      Cron::lcdDisplayDate(utime);
      Cron::dateString(utime,date);
      timeout--;
    } else {
      Cron::lcd.setBacklight(0);
    }
    //Serial.println(date);
    if(Cron::cronTick(utime)) {
    //  Serial.print("Event: ");
    //  Serial.print(Cron::currentEventId);
    //  Serial.print(" State: ");
    //  Serial.println(Cron::state);
    //  Serial.print("Next Event at: ");
    //  Cron::dateString(Cron::nextEventTime, date);
    //  Serial.println(date);
    }
    //Serial.print(millis()-prev);
    //Serial.println("ms");
  }
  if(Serial.available() >= 10) {
    Serial.readBytesUntil('s',in_data,10);
    in_data[10] = NULL;
    utime = strtoul(in_data, NULL, 10);
  }
  byte b = Cron::lcd.readButtons();
  if(b) {
    timeout = DISPLAY_TIMEOUT;
    Cron::lcd.setBacklight(WHITE);
    if(b & BUTTON_SELECT && ((millis() - previous) < 100)) {
      Cron::lcd.setBacklight(RED);
    } else {
      Cron::lcd.setBacklight(WHITE);
    }
    previous = millis();
  }

}


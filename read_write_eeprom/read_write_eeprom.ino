#include <EEPROM.h>

int a = 0;
int value;

void setup()
{
  Serial.begin(57600);
  for(int a = 0; a < 128; a++){
    value = EEPROM.read(a);

    if(a%8 == 0)
      Serial.println("+++++++++++++++");
    Serial.print(a);
    Serial.print("\t");
    Serial.print(value, HEX);
    Serial.println();

  }
  EEPROM.write(0,0);
}

void loop()
{

  delay(500);
}


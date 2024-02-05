#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "ArduinoJson.h"

#define BUZZ_PIN 15
#define LIMIT 50

char ssid[] = "-";  // wifi ssid
char password[] = "-";  // wifi password
String url1 = "-";  // CWA api
String url2 = "-";  // CWA api
HTTPClient http;

Adafruit_MPU6050 mpu;
LiquidCrystal_I2C lcd(0x27, 16, 2);  

float ax_offset, ay_offset, az_offset;
long long count = 0;
long long buffer = 0;
// const unsigned long three_minutes = 1000*3;
clock_t previous_time = clock();
bool delay_info = false;
bool data_printed = false;
int lcddot = 0;

void printvec (float x, float y, float z){
  Serial.printf("%f %f %f", x, y, z);
}

float getacc() {
  /* Get new sensor events with the readings */
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  float ax = a.acceleration.x-ax_offset, ay = a.acceleration.y-ay_offset, az = a.acceleration.z-az_offset;

  // sum the vectors (cm/s^2)
  ax*=100, ay*=100, az*=100;

  double acc = sqrt(pow(ax,2)+pow(ay,2)+pow(az,2));
  Serial.println(acc);
  return acc;
}

String Date_Trans (const char *originTime){
  String output_date = "";
  for (int i = 5; i <= 6; i++) output_date += *(originTime + i);
  output_date += "/";
  for (int i = 8; i <= 9; i++) output_date += *(originTime + i);
  output_date += " ";
  for (int i = 11; i <= 12; i++) output_date += *(originTime + i);
  output_date += ":";
  for (int i = 14; i <= 15; i++) output_date += *(originTime + i);
  return output_date;
}

String Mag_Trans (float magnitudeValue){
  String output_mag = "M:";
  output_mag += String(magnitudeValue, 1);
  output_mag += " I:";
  return output_mag;
}

// input string: MM/DD HH:MM   ex. 12/01 16:15
// 0: None, 1:former, 2:latter
int date_compare (String str_a, String str_b){ 
  time_t now;
  struct tm A;
  struct tm B;
  unsigned long seconds;

  time(&now);  /* get current time; same as: now = time(NULL)  */
  A = *localtime(&now);
  B = *localtime(&now);

  int date_a = (str_a[3]-'0')*10 + (str_a[4]-'0');
  int date_b = (str_b[3]-'0')*10 + (str_b[4]-'0');
  int hour_a = (str_a[6]-'0')*10 + (str_a[7]-'0');
  int hour_b = (str_b[6]-'0')*10 + (str_b[7]-'0');
  int min_a = (str_a[9]-'0')*10 + (str_a[10]-'0');
  int min_b = (str_b[9]-'0')*10 + (str_b[10]-'0');

  A.tm_min = hour_a; A.tm_sec = min_a;
  B.tm_min = hour_b; B.tm_sec = min_b;

  if (difftime(now, mktime(&A) > 86400000 || difftime(now, mktime(&B) > 86400000))){
    if (difftime(now, mktime(&B) <= 86400000)) return 1;
    if (difftime(now, mktime(&B) <= 86400000)) return 2;
    return 0;
  }

  if (difftime(now, mktime(&A)) < difftime(now, mktime(&B))) return 1;
  return 2;
}

void print_info (){
  if (!delay_info) return;
  clock_t current_time = clock(); // to get current time
  if (difftime(current_time, previous_time) < 180000) return;
  delay_info = false;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("fetching data...");

  /* api works */
  String output_date_1, output_mag_1, output_date_2, output_mag_2;
  
  // filter
  StaticJsonDocument<176> filter;
  JsonObject filter_records_Earthquake_0 = filter["records"]["Earthquake"].createNestedObject();
  JsonObject filter_records_Earthquake_0_EarthquakeInfo = filter_records_Earthquake_0.createNestedObject("EarthquakeInfo");
  filter_records_Earthquake_0_EarthquakeInfo["OriginTime"] = true;
  filter_records_Earthquake_0_EarthquakeInfo["EarthquakeMagnitude"]["MagnitudeValue"] = true;
  filter_records_Earthquake_0["Intensity"]["ShakingArea"][0]["AreaIntensity"] = true;

  http.useHTTP10(true);

  // first url
  http.begin(url1); 
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    DynamicJsonDocument doc(800);
    DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;
    }

    const char* originTime1 = doc["records"]["Earthquake"][0]["EarthquakeInfo"]["OriginTime"];
    float magnitudeValue1 = doc["records"]["Earthquake"][0]["EarthquakeInfo"]["EarthquakeMagnitude"]["MagnitudeValue"];
    output_date_1 = Date_Trans(originTime1);
    output_mag_1 = Mag_Trans(magnitudeValue1);

    bool in_TPE = false;
    for (JsonObject records_Earthquake_0_Intensity_ShakingArea_item : doc["records"]["Earthquake"][0]["Intensity"]["ShakingArea"].as<JsonArray>()) {
      const char* areaIntensity1 = records_Earthquake_0_Intensity_ShakingArea_item["AreaIntensity"];
      in_TPE = true;
      output_mag_1 += areaIntensity1;
      if (*(areaIntensity1 + 1) != '級') output_mag_1 += (*(areaIntensity1 + 1) == '強') ? 'S' : 'W';
      break;
    }
    if (!in_TPE) output_mag_1 += "None";
    Serial.println("first data:");
    Serial.println(output_date_1);
    Serial.println(output_mag_1);
  }
  else { 
    Serial.println("Error on HTTP request");
  }
  http.end();

  delay(10000);

  // second url
  http.begin(url2); 
  httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    DynamicJsonDocument doc(800);
    DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      return;
    }

    const char* originTime2 = doc["records"]["Earthquake"][0]["EarthquakeInfo"]["OriginTime"];
    float magnitudeValue2 = doc["records"]["Earthquake"][0]["EarthquakeInfo"]["EarthquakeMagnitude"]["MagnitudeValue"];
    Serial.println(originTime2);
    Serial.println(magnitudeValue2);

    output_date_2 = Date_Trans(originTime2);
    output_mag_2 = Mag_Trans(magnitudeValue2);
    bool in_TPE = false;

    for (JsonObject records_Earthquake_0_Intensity_ShakingArea_item : doc["records"]["Earthquake"][0]["Intensity"]["ShakingArea"].as<JsonArray>()) {
      const char* areaIntensity2 = records_Earthquake_0_Intensity_ShakingArea_item["AreaIntensity"];
      in_TPE = true;
      output_mag_2 += areaIntensity2;
      if (*(areaIntensity2 + 1) != '級') output_mag_2 += (*(areaIntensity2 + 2) == '強') ? 'S' : 'W';
      break;
    }
    if (!in_TPE) output_mag_2 += "None";
    Serial.println("second data:");
    Serial.println(output_date_2);
    Serial.println(output_mag_2);
  }
  else { 
    Serial.println("Error on HTTP request");
  }
  http.end();

  switch(date_compare(output_date_1, output_date_2)){
    case 0: // date unavailable
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Data");
      lcd.setCursor(0, 1);
      lcd.print("Unavailable");
      break;
    case 1:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(output_date_1);
      lcd.setCursor(0, 1);
      lcd.print(output_mag_1);
      break;
    case 2:
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(output_date_2);
      lcd.setCursor(0, 1);
      lcd.print(output_mag_2);
      break;
  }
  data_printed = true;
  lcddot = 0;
}

void setup(void) {
  Serial.begin(115200);
  while(!Serial)
    delay(10); // will pause Zero, Leonardo, etc.. until serial console opens
  Serial.println("serial ready");

  // initialize
  pinMode(BUZZ_PIN, OUTPUT);  // sound

  // LCD things
  lcd.init();
  // turn on LCD backlight                      
  lcd.backlight();

  // mpu things
  if(!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while(1) {
      delay(10);
    }
  }
  Serial.println("MPU6050 Found!");

  // Sets the accelerometer measurement range.
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  // Sets the gyroscope measurement range.
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  // Sets the bandwidth of the Digital Low-Pass Filter.
  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);

  count = 0;
  buffer = 0;

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float ax_temp=0.0, ay_temp=0.0, az_temp=0.0;
  ax_offset=a.acceleration.x, ay_offset=a.acceleration.y, az_offset=a.acceleration.z;
  /* calibration (5min) */
  for(int i=1; i<=3000;i++) {
    ax_offset += a.acceleration.x, ay_offset += a.acceleration.y, az_offset += a.acceleration.z;
    ax_offset /= 2, ay_offset /= 2, az_offset /= 2;
    Serial.println(i);

    lcddot += 1;
    if(lcddot/10 > 2) lcddot -= 30;
    switch(lcddot/10){
      case 0:
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Calibrating.");
        break;
      case 1:
        lcd.setCursor(12, 0);
        lcd.print('.');
        break;
      case 2:
        lcd.setCursor(13, 0);
        lcd.print('.');
        break;
    }

    delay(100);
  }
  lcddot = 0;
  Serial.print("offsets: ");
  printvec(ax_offset, ay_offset, az_offset);
  Serial.println(" m/s^2");
  
  
  // wifi connection
  Serial.print("開始連線到無線網路SSID: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("No Connection");
    Serial.print(".");
    delay(1000);
  }
  Serial.println("連線完成");

  delay(100);
}


void loop() {
  lcddot += 1;
  if(!data_printed){
    if(lcddot/20 > 2) lcddot -= 60;
    switch(lcddot/20){
      case 0:
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Sensoring.");
        break;
      case 1:
        lcd.setCursor(10, 0);
        lcd.print('.');
        break;
      case 2:
        lcd.setCursor(11, 0);
        lcd.print('.');
        break;
    }
  }else{
    if(lcddot > 12000){  // 10min
      data_printed = false;
      lcddot = 0;
    }
  }

  print_info();
  float acc = getacc();
  if (acc >= LIMIT){
    count++;
    while (count >= 40){
      for (int i = 0; i < 3; i++){
        digitalWrite(BUZZ_PIN, HIGH);
        delay(500);
        digitalWrite(BUZZ_PIN, LOW);
        delay(500);
      }
      float tmpacc = getacc();
      if (tmpacc < LIMIT){
        buffer = 0;
        count = 0;
        delay_info = true;
        previous_time = clock();
      }
    }
  }else{
    buffer++;
    if (buffer >= 2){
      count = 0;
      buffer = 0;
    }
  }
  
  delay(50);
}

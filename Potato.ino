#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>

// ตั้งค่า WiFi
const char* ssid = "Stupid";
const char* password = "Delomy2547";

// ตั้งค่า Server URL
const char* serverUrl = "http://154.215.14.103/relay/relay_api.php";

// ตั้งค่า LCD I2C (Address 0x27 หรือ 0x3F)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// กำหนดขา GPIO สำหรับรีเลย์ (ESP32)
const int MOTOR_LEFT_PIN = 16;   // รีเลย์ 1 - มอเตอร์หมุนซ้าย
const int MOTOR_RIGHT_PIN = 17;  // รีเลย์ 2 - มอเตอร์หมุนขวา
const int LINEAR_OUT_PIN = 18;   // รีเลย์ 3 - Linear ดันออก
const int LINEAR_IN_PIN = 19;    // รีเลย์ 4 - Linear ดันเข้า

// กำหนดขา GPIO สำหรับปุ่มกด (ใช้ Pull-up ภายใน)
const int BUTTON_MOTOR_LEFT = 12;   // ปุ่ม 1 - หมุนซ้าย
const int BUTTON_MOTOR_RIGHT = 13;  // ปุ่ม 2 - หมุนขวา
const int BUTTON_LINEAR_OUT = 14;   // ปุ่ม 3 - Linear ดันออก
const int BUTTON_LINEAR_IN = 27;    // ปุ่ม 4 - Linear ดันเข้า

// กำหนดขา GPIO สำหรับ LED แสดงสถานะ
const int LED_MOTOR_LEFT = 25;      // LED มอเตอร์ซ้าย
const int LED_MOTOR_RIGHT = 26;     // LED มอเตอร์ขวา
const int LED_LINEAR_OUT = 32;      // LED Linear ออก
const int LED_LINEAR_IN = 33;       // LED Linear เข้า

// ตัวแปรสำหรับ Debounce
unsigned long lastDebounceTime[4] = {0, 0, 0, 0};
const unsigned long debounceDelay = 50;

// สถานะปุ่มก่อนหน้า (สำหรับ debounce)
int lastButtonState[4] = {HIGH, HIGH, HIGH, HIGH};
int buttonState[4] = {HIGH, HIGH, HIGH, HIGH};

// สถานะการทำงาน
bool motorLeftActive = false;
bool motorRightActive = false;
bool linearOutActive = false;
bool linearInActive = false;

// ตัวแปรสำหรับ Linear Auto-Push
unsigned long linearStartTime = 0;
const unsigned long LINEAR_PUSH_DURATION = 20000;  // เวลาดัน Linear (20 วินาที)
bool linearAutoMode = false;
bool linearDirection = false; // false = OUT, true = IN

// ตัวแปรสำหรับอัพเดท LCD
unsigned long lastLCDUpdate = 0;
const unsigned long LCD_UPDATE_INTERVAL = 100; // อัพเดททุก 100ms

void setup() {
  Serial.begin(115200);
  delay(10);
  
  // ตั้งค่าขาของรีเลย์เป็น OUTPUT
  pinMode(MOTOR_LEFT_PIN, OUTPUT);
  pinMode(MOTOR_RIGHT_PIN, OUTPUT);
  pinMode(LINEAR_OUT_PIN, OUTPUT);
  pinMode(LINEAR_IN_PIN, OUTPUT);
  
  // ตั้งค่าขาของปุ่มเป็น INPUT_PULLUP
  pinMode(BUTTON_MOTOR_LEFT, INPUT_PULLUP);
  pinMode(BUTTON_MOTOR_RIGHT, INPUT_PULLUP);
  pinMode(BUTTON_LINEAR_OUT, INPUT_PULLUP);
  pinMode(BUTTON_LINEAR_IN, INPUT_PULLUP);
  
  // ตั้งค่าขาของ LED เป็น OUTPUT
  pinMode(LED_MOTOR_LEFT, OUTPUT);
  pinMode(LED_MOTOR_RIGHT, OUTPUT);
  pinMode(LED_LINEAR_OUT, OUTPUT);
  pinMode(LED_LINEAR_IN, OUTPUT);
  
  // ปิดรีเลย์และ LED ทั้งหมดเริ่มต้น
  turnOffAll();
  
  // เริ่มต้น LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Starting");
  lcd.setCursor(0, 1);
  lcd.print("Please wait...");
  
  // เชื่อมต่อ WiFi
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed");
    lcd.setCursor(0, 1);
    lcd.print("Offline Mode");
    delay(2000);
  }
  
  updateLCDStatus();
  Serial.println("System ready!");
}

void loop() {
  // ตรวจสอบปุ่มกด
  checkButtons();
  
  // ตรวจสอบ Linear Auto-Push
  checkLinearAutoMode();
  
  // อัพเดท LCD แบบ Real-time เมื่ออยู่ใน Auto Mode
  if (linearAutoMode && (millis() - lastLCDUpdate >= LCD_UPDATE_INTERVAL)) {
    lastLCDUpdate = millis();
    updateLCDStatus();
  }
  
  // ตรวจสอบคำสั่งจาก Server
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck >= 1000) {
    lastCheck = millis();
    
    if (WiFi.status() == WL_CONNECTED) {
      checkForCommands();
    } else {
      static unsigned long lastReconnect = 0;
      if (millis() - lastReconnect >= 30000) { // ลองเชื่อมต่อใหม่ทุก 30 วินาที
        lastReconnect = millis();
        Serial.println("Attempting WiFi reconnection...");
        WiFi.disconnect();
        WiFi.begin(ssid, password);
      }
    }
  }
  
  delay(10);
}

void checkButtons() {
  int buttons[] = {BUTTON_MOTOR_LEFT, BUTTON_MOTOR_RIGHT, BUTTON_LINEAR_OUT, BUTTON_LINEAR_IN};
  
  for (int i = 0; i < 4; i++) {
    int reading = digitalRead(buttons[i]);
    
    if (reading != lastButtonState[i]) {
      lastDebounceTime[i] = millis();
    }
    
    if ((millis() - lastDebounceTime[i]) > debounceDelay) {
      // ถ้าสถานะเปลี่ยนจริง
      if (reading != buttonState[i]) {
        buttonState[i] = reading;
        
        // ปุ่ม XB2-EA142 เป็นแบบ Maintained (กดติด-กดดับ)
        // เมื่อกดติด = LOW, เมื่อกดดับ = HIGH
        
        if (reading == LOW) {
          // ปุ่มถูกกดติด
          handleButtonOn(i);
        } else {
          // ปุ่มถูกกดดับ
          handleButtonOff(i);
        }
      }
    }
    
    lastButtonState[i] = reading;
  }
}

void handleButtonOn(int buttonIndex) {
  switch (buttonIndex) {
    case 0: // ปุ่ม 1 กดติด - เปิดมอเตอร์ซ้าย
      if (!motorRightActive) { // ป้องกันการหมุน 2 ทิศทางพร้อมกัน
        Serial.println("Button 1 ON: Motor LEFT - ACTIVE");
        motorLeftActive = true;
        digitalWrite(MOTOR_LEFT_PIN, LOW);
        digitalWrite(LED_MOTOR_LEFT, HIGH);
        updateLCDStatus();
        sendStatusToServer(1, "on");
      } else {
        Serial.println("Cannot start Motor LEFT - Motor RIGHT is running");
      }
      break;
      
    case 1: // ปุ่ม 2 กดติด - เปิดมอเตอร์ขวา
      if (!motorLeftActive) { // ป้องกันการหมุน 2 ทิศทางพร้อมกัน
        Serial.println("Button 2 ON: Motor RIGHT - ACTIVE");
        motorRightActive = true;
        digitalWrite(MOTOR_RIGHT_PIN, LOW);
        digitalWrite(LED_MOTOR_RIGHT, HIGH);
        updateLCDStatus();
        sendStatusToServer(2, "on");
      } else {
        Serial.println("Cannot start Motor RIGHT - Motor LEFT is running");
      }
      break;
      
    case 2: // ปุ่ม 3 กดติด - เริ่ม Linear Auto
      if (!linearAutoMode && !linearInActive && !linearOutActive) {
        Serial.println("Button 3 ON: Linear AUTO - Starting sequence");
        startLinearAutoSequence();
      } else {
        Serial.println("Linear is already running");
      }
      break;
      
    case 3: // ปุ่ม 4 กดติด - Emergency Stop
      Serial.println("Button 4 ON: Emergency STOP activated");
      if (linearAutoMode) {
        stopLinearAuto();
      }
      break;
  }
}

void handleButtonOff(int buttonIndex) {
  switch (buttonIndex) {
    case 0: // ปุ่ม 1 กดดับ - ปิดมอเตอร์ซ้าย
      if (motorLeftActive) {
        Serial.println("Button 1 OFF: Motor LEFT - STOPPED");
        motorLeftActive = false;
        digitalWrite(MOTOR_LEFT_PIN, HIGH);
        digitalWrite(LED_MOTOR_LEFT, LOW);
        updateLCDStatus();
        sendStatusToServer(1, "off");
      }
      break;
      
    case 1: // ปุ่ม 2 กดดับ - ปิดมอเตอร์ขวา
      if (motorRightActive) {
        Serial.println("Button 2 OFF: Motor RIGHT - STOPPED");
        motorRightActive = false;
        digitalWrite(MOTOR_RIGHT_PIN, HIGH);
        digitalWrite(LED_MOTOR_RIGHT, LOW);
        updateLCDStatus();
        sendStatusToServer(2, "off");
      }
      break;
      
    case 2: // ปุ่ม 3 กดดับ - ไม่ทำอะไร (ใช้สำหรับ Auto mode)
      Serial.println("Button 3 OFF");
      break;
      
    case 3: // ปุ่ม 4 กดดับ - ยกเลิก Emergency Stop
      Serial.println("Button 4 OFF: Emergency STOP released");
      break;
  }
}

void startLinearAutoSequence() {
  linearAutoMode = true;
  linearDirection = false; // เริ่มที่ OUT
  linearStartTime = millis();
  
  // เริ่มดันออก
  linearOutActive = true;
  digitalWrite(LINEAR_OUT_PIN, LOW);
  digitalWrite(LED_LINEAR_OUT, HIGH);
  
  Serial.println("=== LINEAR AUTO SEQUENCE STARTED ===");
  Serial.println("Phase 1: Pushing OUT...");
  updateLCDStatus();
  sendStatusToServer(3, "auto_out");
}

void checkLinearAutoMode() {
  if (!linearAutoMode) return;
  
  unsigned long elapsed = millis() - linearStartTime;
  
  if (!linearDirection) {
    // Phase 1: ดันออก
    if (elapsed >= LINEAR_PUSH_DURATION) {
      // หยุดดันออก
      digitalWrite(LINEAR_OUT_PIN, HIGH);
      digitalWrite(LED_LINEAR_OUT, LOW);
      linearOutActive = false;
      
      // เริ่มดันเข้า
      linearDirection = true;
      linearStartTime = millis();
      linearInActive = true;
      digitalWrite(LINEAR_IN_PIN, LOW);
      digitalWrite(LED_LINEAR_IN, HIGH);
      
      Serial.println("Phase 1 Complete!");
      Serial.println("Phase 2: Pushing IN...");
      updateLCDStatus();
      sendStatusToServer(4, "auto_in");
    }
  } else {
    // Phase 2: ดันเข้า
    if (elapsed >= LINEAR_PUSH_DURATION) {
      // หยุดดันเข้า
      digitalWrite(LINEAR_IN_PIN, HIGH);
      digitalWrite(LED_LINEAR_IN, LOW);
      linearInActive = false;
      linearAutoMode = false;
      
      Serial.println("Phase 2 Complete!");
      Serial.println("=== LINEAR AUTO SEQUENCE FINISHED ===");
      updateLCDStatus();
      sendStatusToServer(4, "off");
    }
  }
}

void stopLinearAuto() {
  linearAutoMode = false;
  
  // ปิด Linear ทั้งหมด
  digitalWrite(LINEAR_OUT_PIN, HIGH);
  digitalWrite(LINEAR_IN_PIN, HIGH);
  digitalWrite(LED_LINEAR_OUT, LOW);
  digitalWrite(LED_LINEAR_IN, LOW);
  
  linearOutActive = false;
  linearInActive = false;
  
  Serial.println("=== LINEAR EMERGENCY STOP ===");
  updateLCDStatus();
  sendStatusToServer(3, "emergency_stop");
}

void turnOffAll() {
  digitalWrite(MOTOR_LEFT_PIN, HIGH);
  digitalWrite(MOTOR_RIGHT_PIN, HIGH);
  digitalWrite(LINEAR_OUT_PIN, HIGH);
  digitalWrite(LINEAR_IN_PIN, HIGH);
  
  digitalWrite(LED_MOTOR_LEFT, LOW);
  digitalWrite(LED_MOTOR_RIGHT, LOW);
  digitalWrite(LED_LINEAR_OUT, LOW);
  digitalWrite(LED_LINEAR_IN, LOW);
}

void updateLCDStatus() {
  lcd.clear();
  
  // บรรทัดที่ 1: สถานะ Linear พร้อมนับถอยหลัง
  lcd.setCursor(0, 0);
  if (linearAutoMode) {
    unsigned long elapsed = millis() - linearStartTime;
    unsigned long remaining = LINEAR_PUSH_DURATION - elapsed;
    int seconds = remaining / 1000;
    
    if (!linearDirection) {
      lcd.print("Linear OUT: ");
      if (seconds < 10) {
        lcd.print(" ");
      }
      lcd.print(seconds);
      lcd.print("s");
    } else {
      lcd.print("Linear IN : ");
      if (seconds < 10) {
        lcd.print(" ");
      }
      lcd.print(seconds);
      lcd.print("s");
    }
    
    // แสดงข้อมูลใน Serial Monitor ด้วย
    Serial.print("Linear ");
    Serial.print(!linearDirection ? "OUT" : "IN");
    Serial.print(": ");
    Serial.print(seconds);
    Serial.println(" seconds remaining");
  } else {
    lcd.print("Linear: IDLE");
  }
  
  // บรรทัดที่ 2: สถานะ Motor
  lcd.setCursor(0, 1);
  if (motorLeftActive) {
    lcd.print("Motor: LEFT ");
  } else if (motorRightActive) {
    lcd.print("Motor: RIGHT");
  } else {
    lcd.print("Motor: IDLE ");
  }
}

void sendStatusToServer(int relayId, String status) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  
  HTTPClient http;
  
  String url = String(serverUrl) + "?action=update_status&relay_id=" + 
               String(relayId) + "&status=" + status;
  
  http.begin(url);
  http.setTimeout(3000);
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    Serial.print("Status sent: Relay ");
    Serial.print(relayId);
    Serial.print(" = ");
    Serial.println(status);
  }
  
  http.end();
}

void checkForCommands() {
  HTTPClient http;
  
  String url = String(serverUrl) + "?arduino=get_commands";
  http.begin(url);
  http.setTimeout(5000);
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      http.end();
      return;
    }
    
    JsonArray commands = doc["commands"];
    
    for (JsonObject cmd : commands) {
      int commandId = cmd["id"];
      int relayId = cmd["relay_id"];
      String command = cmd["command"].as<String>();
      
      Serial.println("===================================");
      Serial.print("Server command ID: ");
      Serial.println(commandId);
      Serial.print("Relay: ");
      Serial.println(relayId);
      Serial.print("Command: ");
      Serial.println(command);
      
      executeCommand(relayId, command);
      markCommandExecuted(commandId);
      
      delay(100);
    }
  }
  
  http.end();
}

void executeCommand(int relayId, String command) {
  switch (relayId) {
    case 1: // Motor Left
      if (command == "on") {
        motorLeftActive = true;
        digitalWrite(MOTOR_LEFT_PIN, LOW);
        digitalWrite(LED_MOTOR_LEFT, HIGH);
      } else {
        motorLeftActive = false;
        digitalWrite(MOTOR_LEFT_PIN, HIGH);
        digitalWrite(LED_MOTOR_LEFT, LOW);
      }
      break;
      
    case 2: // Motor Right
      if (command == "on") {
        motorRightActive = true;
        digitalWrite(MOTOR_RIGHT_PIN, LOW);
        digitalWrite(LED_MOTOR_RIGHT, HIGH);
      } else {
        motorRightActive = false;
        digitalWrite(MOTOR_RIGHT_PIN, HIGH);
        digitalWrite(LED_MOTOR_RIGHT, LOW);
      }
      break;
      
    case 3: // Linear Auto
      if (command == "auto_start") {
        startLinearAutoSequence();
      } else if (command == "stop") {
        stopLinearAuto();
      }
      break;
  }
  
  updateLCDStatus();
}

void markCommandExecuted(int commandId) {
  HTTPClient http;
  
  String url = String(serverUrl) + "?arduino=mark_executed&command_id=" + String(commandId);
  http.begin(url);
  http.setTimeout(5000);
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    Serial.print("Command ");
    Serial.print(commandId);
    Serial.println(" marked as executed");
  }
  
  http.end();
}
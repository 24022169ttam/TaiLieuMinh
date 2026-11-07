#define BLYNK_TEMPLATE_ID "TMPL6S8ZwTy3Y"
#define BLYNK_TEMPLATE_NAME "Cua Chong Ngap"
#define BLYNK_AUTH_TOKEN "aFI3J6RAVe6waQMpR2pEI3Lx1BQg8TnU"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

// Thay đổi tên Wi-Fi và mật khẩu của bạn tại đây
char ssid[] = "TP-Link_D502";
char pass[] = "68739164";

// Định nghĩa các chân kết nối trên ESP32
#define TRIG_PIN          5   // Cảm biến siêu âm - Trig
#define ECHO_PIN         18   // Cảm biến siêu âm - Echo
#define WATER_SENSOR_PIN 34   // Cảm biến mực nước (Chân Analog)
#define IR_PIN           19   // Cảm biến hồng ngoại phát hiện xe
#define SERVO_PIN        13   // Động cơ Servo điều khiển cửa (PWM)
#define BUZZER_PIN       12   // Còi báo động

#define LED_GREEN         2   // Đèn Xanh: An toàn
#define LED_YELLOW        4   // Đèn Vàng: Nguy cơ ngập
#define LED_RED          16   // Đèn Đỏ: Ngập sâu / Đóng cửa

#define SW_AUTO          14   // Công tắc chọn chế độ phần cứng (Auto/Manual)
#define SW_OPEN          27   // Nút gạt mở cửa thủ công phần cứng
#define SW_CLOSE         26   // Nút gạt đóng cửa thủ công phần cứng

// Khởi tạo đối tượng LCD và Servo
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo doorServo;

// Biến lưu trạng thái khóa cửa khi đã đóng do ngập sâu
bool doorLockedAfterFlood = false;

// Biến nhận lệnh từ App Blynk (V0: Chọn chế độ Auto/Manual, V1: Nút mở, V2: Nút đóng)
int appAutoMode = 1;
int appOpenCmd = 0;
int appCloseCmd = 0;

BLYNK_WRITE(V0) { appAutoMode = param.asInt(); }
BLYNK_WRITE(V1) { appOpenCmd = param.asInt(); }
BLYNK_WRITE(V2) { appCloseCmd = param.asInt(); }

// Biến điều khiển chớp nháy độc lập cho đèn Vàng (~500ms)
unsigned long previousMillisYellow = 0;
const long intervalYellow = 500; 
bool ledYellowState = LOW;

// Biến điều khiển chớp nháy độc lập cho đèn Đỏ (~150ms)
unsigned long previousMillisRed = 0;
const long intervalRed = 150; 
bool ledRedState = LOW;

// Biến điều khiển còi kêu dồn dập nhanh (~80ms)
unsigned long previousMillisBuzzer = 0;
const long intervalBuzzer = 80; 
bool buzzerState = LOW;

// Hàm đo khoảng cách từ cảm biến siêu âm (từ trên chiếu xuống mặt nước)
long getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH);
  long distance = duration * 0.034 / 2; // Tính ra cm
  return distance;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Cấu hình chân vào/ra
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(WATER_SENSOR_PIN, INPUT);
  pinMode(IR_PIN, INPUT_PULLUP);
  
  pinMode(SW_AUTO, INPUT_PULLUP);
  pinMode(SW_OPEN, INPUT_PULLUP);
  pinMode(SW_CLOSE, INPUT_PULLUP);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);

  // Khởi động Servo và màn hình LCD
  doorServo.attach(SERVO_PIN);
  doorServo.write(0); // Vị trí ban đầu: Mở cửa (0 độ)

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("He Thong Chong Ngap");
  lcd.setCursor(0, 1);
  lcd.print(" Dang ket noi WiFi");

  // Kết nối Wi-Fi trước
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");

  lcd.clear();
  lcd.print("Dang ket noi Blynk");

  // Kết nối Blynk trỏ chuẩn về Server Singapore (SGP1) kèm cổng 80
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass, "sgp1.blynk.cloud", 80);

  lcd.clear();
  lcd.print("Ket noi thanh cong!");
  delay(1500);
  lcd.clear();
}

void loop() {
  Blynk.run(); // Duy trì kết nối Blynk

  // Kiểm tra chế độ (Cho phép App linh hoạt điều khiển)
  // Đọc trạng thái công tắc cứng (LOW là gạt sang chế độ Auto, tùy vào cách bạn đấu nối)
  bool hardwareAuto = (digitalRead(SW_AUTO) == LOW); 

  // Hệ thống sẽ chạy Auto khi: Cả công tắc cứng ở chế độ Auto VÀ App cũng đang bật Auto
  // (Hoặc bạn có thể chọn ưu tiên công tắc cứng bằng cách sửa lại logic ở đây)
  bool isAuto = hardwareAuto && (appAutoMode == 1);
  bool carDetected = (digitalRead(IR_PIN) == LOW); // Đọc trạng thái cảm biến vật cản (LOW: có vật cản/xe)

  int waterVal = analogRead(WATER_SENSOR_PIN);     // Đọc cảm biến mực nước Analog
  long distance = getDistance();                 // Đọc cảm biến siêu âm
  unsigned long currentMillis = millis();

  // Đồng bộ thông số lên App Blynk 
  // V3: Mực nước, V4: Khoảng cách, V5: Trạng thái vật cản (0/1)
  Blynk.virtualWrite(V3, waterVal);
  Blynk.virtualWrite(V4, distance);
  Blynk.virtualWrite(V5, carDetected ? 1 : 0);

  if (isAuto) {
    // --- CHẾ ĐỘ TỰ ĐỘNG ---
    lcd.setCursor(0, 0);
    lcd.print("D:");
    lcd.print(distance);
    lcd.print(" W:");
    lcd.print(waterVal);
    lcd.print(" IR:");
    lcd.print(carDetected ? "1" : "0");
    lcd.print(" ");

    // Xác định trạng thái mực nước
    bool isDeepFlood = (distance <7.0 )|(waterVal >= 2000);
    bool isWarning = ((distance >= 7.0 && distance < 7.5) || (waterVal >= 500 && waterVal < 2000)) && !isDeepFlood;

    // --- 1. MỨC NGẬP SÂU (ƯU TIÊN CAO NHẤT) ---
    if (isDeepFlood) {
      doorLockedAfterFlood = true; 

      lcd.setCursor(0, 1);
      if (carDetected) {
        lcd.print("NGAP! CO XE KET! ");
        doorServo.write(0); // Giữ mở cửa nếu kẹt xe
        Blynk.virtualWrite(V6, "NGAP! CO XE KET!"); // Gửi trạng thái lên App
      } else {
        lcd.print("NGAP SAU! DONG CUA");
        doorServo.write(140); // Đóng cửa chống ngập
        Blynk.virtualWrite(V6, "NGAP SAU! DONG");   // Gửi trạng thái lên App
      }

      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_YELLOW, LOW);

      // Còi kêu dồn dập
      if (currentMillis - previousMillisBuzzer >= intervalBuzzer) {
        previousMillisBuzzer = currentMillis;
        buzzerState = !buzzerState;
        digitalWrite(BUZZER_PIN, buzzerState);
      }

      // Đèn Đỏ chớp nhanh
      if (currentMillis - previousMillisRed >= intervalRed) {
        previousMillisRed = currentMillis;
        ledRedState = !ledRedState;
        digitalWrite(LED_RED, ledRedState);
      }
    } 
    // --- 2. MỨC NGUY CƠ NGẬP ---
    else if (isWarning) {
      lcd.setCursor(0, 1);
      lcd.print("CANH BAO: NGUY CO ");
      Blynk.virtualWrite(V6, "CANH BAO NGUY CO"); // Gửi trạng thái lên App
      
      if (!doorLockedAfterFlood) {
        doorServo.write(0); 
      }

      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_RED, LOW);
      digitalWrite(BUZZER_PIN, LOW); // Tắt còi ở mức nguy cơ

      // Đèn Vàng chớp nháy bình thường (500ms)
      if (currentMillis - previousMillisYellow >= intervalYellow) {
        previousMillisYellow = currentMillis;
        ledYellowState = !ledYellowState;
        digitalWrite(LED_YELLOW, ledYellowState);
      }
    } 
    // --- 3. MỨC AN TOÀN (NƯỚC ĐÃ RÚT HOẶC BÌNH THƯỜNG) ---
    else {
      lcd.setCursor(0, 1);
      
      if (doorLockedAfterFlood) {
        lcd.print("NUOC RUT! CHO MO  ");
        doorServo.write(140); 
        Blynk.virtualWrite(V6, "NUOC RUT! CHO MO"); // Gửi trạng thái lên App
        
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_YELLOW, LOW);
        digitalWrite(LED_RED, HIGH);
        digitalWrite(BUZZER_PIN, LOW);
      } 
      else {
        if (carDetected) {
          lcd.print("Co xe qua         ");
          Blynk.virtualWrite(V6, "Co xe qua cua");     // Gửi trạng thái lên App
        } else {
          lcd.print("Trang Thai: AN TOAN");
          Blynk.virtualWrite(V6, "AN TOAN");          // Gửi trạng thái lên App
        }
        
        doorServo.write(0); // Mở cửa
        digitalWrite(LED_GREEN, HIGH);
        digitalWrite(LED_YELLOW, LOW);
        digitalWrite(LED_RED, LOW);
        digitalWrite(BUZZER_PIN, LOW);
      }
    }

  } 
  else {
    // --- CHẾ ĐỘ THỦ CÔNG (MANUAL) ---
    lcd.setCursor(0, 0);
    lcd.print("THU CONG | IR:");
    lcd.print(carDetected ? "1" : "0");
    lcd.print("  ");
    
    Blynk.virtualWrite(V6, "CHE DO THU CONG"); // Gửi trạng thái lên App
    // Tắt toàn bộ đèn và còi ở chế độ thủ công
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    // Kiểm tra lệnh đóng từ nút cứng hoặc nút V2 trên App
    if (digitalRead(SW_CLOSE) == LOW || appCloseCmd == 1) {
      doorServo.write(140); // Đóng cửa
      lcd.setCursor(0, 1);
      lcd.print("Cua: DONG         ");
    } 
    // Kiểm tra lệnh mở từ nút cứng hoặc nút V1 trên App
    else if (digitalRead(SW_OPEN) == LOW || appOpenCmd == 1) {
      doorServo.write(0);  // Mở cửa
      lcd.setCursor(0, 1);
      lcd.print("Cua: MO           ");
      doorLockedAfterFlood = false; // Xóa trạng thái khóa khi chủ động mở
    }
  }
  
  delay(50);
}
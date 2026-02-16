#include <M5Unified.h>
#include <ESP32Servo.h>

const int FSR_PIN   = 36;
const int SOL_PIN   = 21;
const int SERVO_PIN = 26;

Servo servo;

// ===== サーボ（入れ替え版前提）=====
int SERVO_STOP_US = 1500;   // ←完全停止点に調整
int SERVO_ON_US   = 1250;   // ←押した時に回る側（入れ替え版で効いた値）

// ===== FSR判定（基準からの差：極性不問）=====
const int DELTA_ON  = 120;
const int DELTA_OFF = 80;
const uint16_t STABLE_MS = 120;

// ===== タイミング =====
const uint32_t IDLE_STOP_MS = 5UL * 60UL * 1000UL;  // 5分無圧で睡眠
uint32_t nextCallAt = 0;

// ===== 状態 =====
int base = 0;
bool calibrated = false;
uint32_t calibStart = 0;

bool pressed = false;
uint32_t candOnSince = 0, candOffSince = 0;
uint32_t lastPressureAt = 0;

static inline uint32_t nowMs() { return millis(); }

// ===== 低レベル動作 =====
void servoStop() { servo.writeMicroseconds(SERVO_STOP_US); }

void servoNudge(int ms) {
  servo.writeMicroseconds(SERVO_ON_US);
  delay(ms);
  servoStop();
}

void solOn(int ms) {
  digitalWrite(SOL_PIN, HIGH);
  delay(ms);
  digitalWrite(SOL_PIN, LOW);
}

void solDoubleTap() {
  solOn(70); delay(120);
  solOn(70);
}

void solRhythm_tanTaTaTan() {
  // たん・た・た・たん（長短のリズム）
  solOn(110); delay(140);
  solOn(70);  delay(110);
  solOn(70);  delay(220);
  solOn(120);
}

void scheduleNextCall() {
  // 15〜40秒後にランダム
  nextCallAt = nowMs() + 15000 + random(0, 25000);
}

// ===== リクエスト1：初回（マグカップ置いたら）=====
// ソレノイド → 2秒後にサーボ
void firstGreeting() {
  solDoubleTap();
  delay(2000);         // ★ここがリクエスト1
  servoNudge(350);
}

// ===== リクエスト2：構って動作（2/3 sol, 1/3 motor）=====
void callForAttention() {
  int r = random(0, 3); // 0,1,2
  if (r == 0 || r == 1) {
    // 2/3：ソレノイド
    if (random(0, 2) == 0) solDoubleTap();
    else solRhythm_tanTaTaTan();
  } else {
    // 1/3：モーター
    servoNudge(250);
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.In_I2C.release();

  pinMode(SOL_PIN, OUTPUT);
  digitalWrite(SOL_PIN, LOW);

  servo.setPeriodHertz(50);
  servo.attach(SERVO_PIN, 500, 2500);
  servoStop();

  randomSeed(esp_random());

  calibStart = nowMs();
  lastPressureAt = nowMs();
  scheduleNextCall();

  M5.Display.clear();
  M5.Display.setTextSize(2);
}

void loop() {
  M5.update();

  int fsr = analogRead(FSR_PIN);

  // --- キャリブレーション（最初の2秒は触らない）---
  if (!calibrated) {
    if (base == 0) base = fsr;
    base = (base * 9 + fsr) / 10;

    M5.Display.setCursor(10, 10);
    M5.Display.printf("Calibrating...\nFSR:%4d\nBASE:%4d\n", fsr, base);

    if (nowMs() - calibStart >= 2000) {
      calibrated = true;
      M5.Display.clear();
    }
    delay(20);
    return;
  }

  // --- 押下判定（極性不問＋ヒステリシス）---
  int d = abs(fsr - base);
  bool wantOn  = (d >= DELTA_ON);
  bool wantOff = (d <= DELTA_OFF);

  uint32_t t = nowMs();

  if (wantOn)  { if (!candOnSince)  candOnSince  = t; candOffSince = 0; }
  if (wantOff) { if (!candOffSince) candOffSince = t; candOnSince  = 0; }

  // --- 表示 ---
  M5.Display.setCursor(10, 10);
  M5.Display.printf("FSR:%4d BASE:%4d\n", fsr, base);
  M5.Display.printf("d:%3d  STATE:%s\n", d, pressed ? "ON " : "OFF");
  M5.Display.printf("next:%lus\n", (nextCallAt > t) ? (unsigned long)((nextCallAt - t)/1000) : 0);

  // --- ON確定（初回：ソレノイド→2秒後→サーボ）---
  if (!pressed && candOnSince && (t - candOnSince >= STABLE_MS)) {
    pressed = true;
    candOnSince = candOffSince = 0;

    lastPressureAt = t;
    scheduleNextCall();   // 触られたら次の呼びかけをリセット

    firstGreeting();      // ★リクエスト1
  }

  // --- OFF確定（離した時は基本何もしない）---
  if (pressed && candOffSince && (t - candOffSince >= STABLE_MS)) {
    pressed = false;
    candOnSince = candOffSince = 0;
    servoStop();
  }

  // --- 5分無圧で睡眠 ---
  if (!pressed) {
    if (t - lastPressureAt >= IDLE_STOP_MS) {
      servoStop();
      digitalWrite(SOL_PIN, LOW);
      delay(50);
      return;
    }
  } else {
    lastPressureAt = t;
  }

  // --- 置きっぱなし生命：たまに「かまって」---
  if (pressed && t >= nextCallAt) {
    callForAttention();   // ★リクエスト2
    scheduleNextCall();
  }

  delay(10);
}

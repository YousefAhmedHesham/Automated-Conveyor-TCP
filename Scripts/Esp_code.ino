#include <WiFi.h>

// --- Hardware Pin Definitions ---
const int PIN_ENC_A = 34;
const int PIN_ENC_B = 35;
const int PIN_IR    = 32;
const int PIN_LIMIT = 33;

// Motor Driver Pins (Your Working Pins)
const int PIN_ENA = 25;
const int PIN_IN1 = 22;
const int PIN_IN2 = 23;

// --- Physical Parameters ---
const float ENC_PPR = 374.0;
const float PULLEY_RADIUS_CM = 1.1;
const float PULLEY_CIRCUMFERENCE = 2 * 3.14159 * PULLEY_RADIUS_CM;

// --- Automation Constants ---
const float TARGET_DISTANCE_CM = 25.0;

//  PWM variable
int motorSpeed = 255;

// --- Sequence timing ---
const unsigned long HOME_WAIT_MS = 3000;
const unsigned long POS_WAIT_MS  = 3000;

// --- Limit debounce ---
const uint32_t LIMIT_DEBOUNCE_MS = 30;
int limStable = 0;
int limLastRaw = 0;
uint32_t limLastChangeMs = 0;

// --- State Machine ---
enum CartState { ST_RETURN, ST_WAIT_LOAD, ST_MOVE_OUT, ST_WAIT_UNLOAD };
CartState currentState = ST_RETURN;
String stateString = "RETURNING";
unsigned long stateTimer = 0;

// --- Global Variables ---
volatile long encoderTicks = 0;
long lastEncoderTicks = 0;
long targetTicks = 0;
uint32_t realItemCount = 0;

// --- WiFi Variables ---
const char* ssid = "ELDWAKHLY";
const char* password = "ssmym12345";
const uint16_t PORT = 55001;

// --- Congestion Control Variables ---
uint32_t sendInterval = 500;
const uint32_t MIN_INTERVAL = 200;
const uint32_t MAX_INTERVAL = 5000;
uint32_t lastPacketTxTime = 0;

//  Base interval for recovery
const uint32_t BASE_INTERVAL = 500;

WiFiServer server(PORT);
WiFiClient client;

uint32_t seq = 0;
uint32_t lastStatusMs = 0;
bool waitingAck = false;
uint32_t waitingAckSeq = 0;

// --- ACK Timeout (FAULT + continue) ---
uint32_t waitingAckMs = 0;
const uint32_t ACK_TIMEOUT_MS = 4000;

// --- ACK TIMEOUT spam suppression + backoff ---
bool ackTimeoutLatched = false;
const uint32_t ACK_TIMEOUT_BACKOFF_MIN = 2000;

// --- Real RTT (measured only on ACK) ---
uint32_t lastRttMs = 0;

String rx = "";

// --- Motor Stall Detection ---
long lastEncForStall = 0;
uint32_t lastEncChangeMs = 0;
const uint32_t MOTOR_STALL_MS = 2000;
const float STALL_WIGGLE_CM = 3.0;
long wiggleTicks = 0;

//  Start/Stop flag
bool systemEnabled = true;

//  Target + DONE latch
uint32_t targetTotalItems = 0;
bool doneLatched = false;

//  NEW: IR SENSOR FAULT (stuck active > 2 minutes)
uint32_t irActiveStartMs = 0;
bool irFaultLatched = false;
const uint32_t IR_FAULT_MS = 2000; // 2 s

// --- Functions ---
void IRAM_ATTR readEncoder() {
  if (digitalRead(PIN_ENC_B) == digitalRead(PIN_ENC_A)) encoderTicks++;
  else encoderTicks--;
}

void motorStop() { digitalWrite(PIN_IN1, LOW); digitalWrite(PIN_IN2, LOW); analogWrite(PIN_ENA, 0); }
void motorFwd(int speed) { digitalWrite(PIN_IN1, HIGH); digitalWrite(PIN_IN2, LOW); analogWrite(PIN_ENA, speed); }
void motorRev(int speed) { digitalWrite(PIN_IN1, LOW); digitalWrite(PIN_IN2, HIGH); analogWrite(PIN_ENA, speed); }

void stallRecoveryWiggle() {
  long start = encoderTicks;
  motorFwd(motorSpeed);
  while (abs(encoderTicks - start) < wiggleTicks) { /* blocking */ }
  motorStop();
  start = encoderTicks;
  motorRev(motorSpeed);
  while (abs(encoderTicks - start) < wiggleTicks) { /* blocking */ }
  motorStop();
}

void sendLine(const String& s){
  if (client && client.connected()){
    client.print(s);
    client.print('\n');
  }
}

void sendFault(const char* code){
  String msg = "{\"type\":\"FAULT\",\"ts_ms\":" + String(millis()) +
               ",\"seq\":" + String(++seq) + ",\"code\":\"" + String(code) + "\"}";
  sendLine(msg);
  waitingAck = true; waitingAckSeq = seq;
  waitingAckMs = millis();
}

int parseIntField(const String& line, const char* key, int defaultVal){
  String k = String("\"") + key + "\":";
  int idx = line.indexOf(k);
  if (idx < 0) return defaultVal;
  idx += k.length();
  return line.substring(idx).toInt();
}

String parseStringField(const String& line, const char* key, const String& defaultVal){
  String k = String("\"") + key + "\":\"";
  int idx = line.indexOf(k);
  if (idx < 0) return defaultVal;
  idx += k.length();
  int end = line.indexOf("\"", idx);
  if (end < 0) return defaultVal;
  return line.substring(idx, end);
}

void forceHomingNow(){
  currentState = ST_RETURN;
  stateTimer = millis();
  lastEncForStall = encoderTicks;
  lastEncChangeMs = millis();
}

void enterDoneMode(){
  currentState = ST_RETURN;
  stateTimer = millis();
  lastEncForStall = encoderTicks;
  lastEncChangeMs = millis();
}

// ✅ robust resume from DONE when target increases
void resumeFromDoneIfPossible(){
  if (doneLatched && targetTotalItems > realItemCount) {
    doneLatched = false;
    systemEnabled = true;

    int limNow = digitalRead(PIN_LIMIT);
    if (limNow == HIGH) {
      encoderTicks = 0;
      lastEncoderTicks = 0;
      lastEncForStall = encoderTicks;
      lastEncChangeMs = millis();
      currentState = ST_MOVE_OUT;
    } else {
      forceHomingNow();
    }
  }
}

void handleLine(String line){
  line.trim();

  // CMD handling
  if (line.indexOf("\"type\":\"CMD\"") >= 0) {
    String cmd = parseStringField(line, "cmd", "");
    cmd.toUpperCase();

    if (cmd == "STOP") {
      systemEnabled = false;
      motorStop();
    }
    else if (cmd == "START") {
      systemEnabled = true;
      resumeFromDoneIfPossible();
    }
    else if (cmd == "HOME") {
      systemEnabled = true;
      doneLatched = false;
      forceHomingNow();
    }
    else if (cmd == "PWM") {
      int v = parseIntField(line, "value", motorSpeed);
      if (v < 0) v = 0;
      if (v > 255) v = 255;
      motorSpeed = v;
    }
    else if (cmd == "TARGET") {
      int v = parseIntField(line, "value", 0);
      if (v > 0) targetTotalItems += (uint32_t)v;
      resumeFromDoneIfPossible();
    }
    return;
  }

  // ACK handling (unchanged)
  if (line.indexOf("\"type\":\"ACK\"") >= 0) {
    int idx = line.indexOf("\"ack\":");
    if (idx >= 0) {
      int ackVal = line.substring(idx + 6).toInt();
      if (waitingAck && (uint32_t)ackVal == waitingAckSeq) {

        if (lastPacketTxTime > 0) lastRttMs = millis() - lastPacketTxTime;

        waitingAck = false;
        ackTimeoutLatched = false;

        uint32_t rtt = lastRttMs;

        if (rtt > 1000) {
          sendInterval *= 2;
        }
        else if (rtt < 300 && sendInterval > MIN_INTERVAL) {
          int32_t err = (int32_t)sendInterval - (int32_t)BASE_INTERVAL;
          if (err > 0) {
            uint32_t dec = max((uint32_t)50, (uint32_t)(err / 4));
            sendInterval -= dec;
          } else {
            if (sendInterval > MIN_INTERVAL + 50) sendInterval -= 50;
          }
        }

        if (sendInterval > MAX_INTERVAL) sendInterval = MAX_INTERVAL;
        if (sendInterval < MIN_INTERVAL) sendInterval = MIN_INTERVAL;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_ENC_A, INPUT); pinMode(PIN_ENC_B, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), readEncoder, RISING);
  pinMode(PIN_IR, INPUT);
  pinMode(PIN_LIMIT, INPUT_PULLUP);
  pinMode(PIN_ENA, OUTPUT); pinMode(PIN_IN1, OUTPUT); pinMode(PIN_IN2, OUTPUT);
  motorStop();

  targetTicks = (TARGET_DISTANCE_CM / PULLEY_CIRCUMFERENCE) * ENC_PPR;
  wiggleTicks = (STALL_WIGGLE_CM / PULLEY_CIRCUMFERENCE) * ENC_PPR;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected!");
  Serial.print("ESP IP: "); Serial.println(WiFi.localIP());
  server.begin();

  currentState = ST_RETURN;
  lastEncForStall = encoderTicks;
  lastEncChangeMs = millis();
  limLastRaw = digitalRead(PIN_LIMIT);
  limStable = limLastRaw;
  limLastChangeMs = millis();

  // IR fault init
  irActiveStartMs = 0;
  irFaultLatched = false;
}

void loop() {
  // WiFi accept/read
  if (!client || !client.connected()) {
    client = server.available();
    if (client) {
      sendLine("HELLO");
      waitingAck = false;
      ackTimeoutLatched = false;
      rx = "";
      sendInterval = 500;
      lastRttMs = 0;
      systemEnabled = true;
      doneLatched = false;

      // IR fault reset on new connection (safe)
      irActiveStartMs = 0;
      irFaultLatched = false;
    }
  }

  if (client && client.connected()) {
    while (client.available()) {
      char c = (char)client.read();
      if (c == '\n') { handleLine(rx); rx = ""; } else rx += c;
    }
  }

  // Sensors
  int ir = digitalRead(PIN_IR);
  int limRaw = digitalRead(PIN_LIMIT);
  if (limRaw != limLastRaw) { limLastRaw = limRaw; limLastChangeMs = millis(); }
  if ((millis() - limLastChangeMs) >= LIMIT_DEBOUNCE_MS) { limStable = limLastRaw; }
  int lim = limStable;

  //  IR SENSOR FAULT CHECK
  // "IR detected" means ir == LOW (matches your item counting condition)
  if (ir == LOW) {
    if (irActiveStartMs == 0) irActiveStartMs = millis();
    if (!irFaultLatched && (millis() - irActiveStartMs >= IR_FAULT_MS)) {
      sendFault("IR sensor fault");
      irFaultLatched = true;   // prevent spamming
    }
  } else {
    // IR returned normal -> clear timer + allow future fault again
    irActiveStartMs = 0;
    irFaultLatched = false;
  }

  if (encoderTicks != lastEncForStall) { lastEncForStall = encoderTicks; lastEncChangeMs = millis(); }
  bool motorCommandedMove = false;

  // DONE condition (unchanged)
  if (!doneLatched && targetTotalItems > 0 && realItemCount != 0 && realItemCount >= targetTotalItems) {
    doneLatched = true;
    systemEnabled = true;
    enterDoneMode();
  }

  // STOP / DONE / NORMAL behavior unchanged
  if (!systemEnabled) {
    motorStop();
    stateString = "STOPPED";
    motorCommandedMove = false;
  }
  else if (doneLatched) {
    stateString = "DONE";
    if (lim == HIGH) {
      motorStop();
      motorCommandedMove = false;
    } else {
      motorRev(motorSpeed);
      motorCommandedMove = true;
    }
  }
  else {
    switch (currentState) {
      case ST_RETURN:
        stateString = "HOMING";
        if (lim == HIGH) {
          motorStop(); stateTimer = millis(); currentState = ST_WAIT_LOAD;
          lastEncForStall = encoderTicks; lastEncChangeMs = millis();
        } else { motorRev(motorSpeed); motorCommandedMove = true; }
        break;

      case ST_WAIT_LOAD:
        stateString = "WAIT_HOME";
        motorStop();
        lastEncForStall = encoderTicks; lastEncChangeMs = millis();
        if (millis() - stateTimer >= HOME_WAIT_MS) {
          encoderTicks = 0;
          lastEncForStall = encoderTicks; lastEncChangeMs = millis();
          currentState = ST_MOVE_OUT;
        }
        break;

      case ST_MOVE_OUT:
        stateString = "MOVE_TO_POS";
        if (abs(encoderTicks) >= targetTicks) {
          motorStop(); stateTimer = millis(); currentState = ST_WAIT_UNLOAD;
          lastEncForStall = encoderTicks; lastEncChangeMs = millis();
        } else { motorFwd(motorSpeed); motorCommandedMove = true; }
        break;

      case ST_WAIT_UNLOAD:
        stateString = "WAIT_UNLOAD";
        motorStop();
        lastEncForStall = encoderTicks; lastEncChangeMs = millis();
        if (ir == LOW) { realItemCount++; currentState = ST_RETURN; }
        else if (millis() - stateTimer >= POS_WAIT_MS) { currentState = ST_RETURN; }
        break;
    }
  }

  // Motor stall detection unchanged
  if (systemEnabled && !doneLatched && motorCommandedMove && (millis() - lastEncChangeMs) >= MOTOR_STALL_MS) {
    motorStop();
    sendFault("Motor stall");
    stallRecoveryWiggle();
    currentState = ST_RETURN;
    lastEncForStall = encoderTicks; lastEncChangeMs = millis();
  }

  // Send STATUS unchanged
  if (client && client.connected()) {

    if (waitingAck && (millis() - waitingAckMs > ACK_TIMEOUT_MS)) {
      if (!ackTimeoutLatched) {
        sendFault("ACK TIMEOUT");
        ackTimeoutLatched = true;
      }

      if (sendInterval < ACK_TIMEOUT_BACKOFF_MIN) sendInterval = ACK_TIMEOUT_BACKOFF_MIN;
      else {
        sendInterval *= 2;
        if (sendInterval > MAX_INTERVAL) sendInterval = MAX_INTERVAL;
      }
      waitingAck = false;
    }

    if (!waitingAck && (millis() - lastStatusMs >= sendInterval)) {
      uint32_t dt = millis() - lastStatusMs;
      lastStatusMs = millis();

      long dTicks = encoderTicks - lastEncoderTicks;
      float rpm = ((float)dTicks / ENC_PPR) * (60000.0 / dt);
      lastEncoderTicks = encoderTicks;

      float dist = ((float)encoderTicks / ENC_PPR) * PULLEY_CIRCUMFERENCE;
      int isCongested = (sendInterval > 500) ? 1 : 0;

      seq++;
      String msg = "{";
      msg += "\"type\":\"STATUS\",";
      msg += "\"ts_ms\":" + String(millis()) + ",";
      msg += "\"seq\":" + String(seq) + ",";
      msg += "\"speed_rpm\":" + String((int)rpm) + ",";
      msg += "\"encoder\":" + String(encoderTicks) + ",";
      msg += "\"items\":" + String(realItemCount) + ",";
      msg += "\"limit\":" + String(lim) + ",";
      msg += "\"ir\":" + String(ir) + ",";
      msg += "\"err\":0,";
      msg += "\"state\":\"" + stateString + "\",";
      msg += "\"distance_cm\":" + String(dist, 2) + ",";
      msg += "\"interval\":" + String(sendInterval) + ",";
      msg += "\"congestion\":" + String(isCongested) + ",";
      msg += "\"rtt\":" + String(lastRttMs);
      msg += "}";

      sendLine(msg);
      waitingAck = true;
      waitingAckSeq = seq;
      lastPacketTxTime = millis();
      waitingAckMs = millis();
    }
  }
}

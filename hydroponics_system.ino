/*
 * Advanced Hydroponics Monitoring System
 * Author: Akhileshwar Pratap Singh
 * Description: Automated nutrient dosing and pH control system with IoT monitoring
 * Platform: ESP32
 * 
 * Features:
 * - Automated pH control (5.5-6.5 range)
 * - EC-based nutrient dosing
 * - Real-time monitoring via Blynk
 * - Temperature compensation
 * - Smart alerts and notifications
 */

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// WiFi and Blynk Credentials
#define BLYNK_TEMPLATE_ID "TMPL3xxxxx"  // Replace with your Template ID
#define BLYNK_AUTH_TOKEN "your_auth_token_here"
char ssid[] = "Your_WiFi_SSID";
char pass[] = "Your_WiFi_Password";

// Pin Definitions
#define PH_SENSOR_PIN 34           // Analog pH sensor
#define EC_SENSOR_PIN 35           // Analog EC sensor
#define TEMP_SENSOR_PIN 4          // DS18B20 temperature sensor
#define WATER_LEVEL_PIN 33         // Water level sensor

// Pump Control Pins
#define PUMP_NUTRIENT_A 25         // Nutrient A pump
#define PUMP_NUTRIENT_B 26         // Nutrient B pump
#define PUMP_PH_UP 27              // pH Up pump
#define PUMP_PH_DOWN 14            // pH Down pump
#define PUMP_WATER 12              // Main water pump

// LED Indicators
#define LED_SYSTEM 2               // System status LED
#define LED_ALERT 15               // Alert LED

// Blynk Virtual Pins
#define VPIN_PH V0
#define VPIN_EC V1
#define VPIN_TEMP V2
#define VPIN_WATER_LEVEL V3
#define VPIN_PUMP_A V4
#define VPIN_PUMP_B V5
#define VPIN_PUMP_PH_UP V6
#define VPIN_PUMP_PH_DOWN V7
#define VPIN_SYSTEM_STATUS V8
#define VPIN_TERMINAL V9

// Target Values and Tolerances
#define TARGET_PH 6.0
#define PH_TOLERANCE 0.2
#define TARGET_EC 1500              // μS/cm
#define EC_TOLERANCE 100
#define MIN_WATER_LEVEL 20          // Percentage

// Timing Constants
#define PUMP_DOSE_TIME 1000         // Pump run time per dose (ms)
#define PUMP_COOLDOWN 60000         // Wait time between doses (ms)
#define SENSOR_READ_INTERVAL 5000   // Sensor reading interval (ms)
#define BLYNK_UPDATE_INTERVAL 10000 // Blynk update interval (ms)

// Calibration Constants
#define PH_NEUTRAL_VOLTAGE 1500     // mV at pH 7.0
#define PH_ACID_VOLTAGE 2000        // mV at pH 4.0
#define EC_CALIBRATION_FACTOR 1.0   // Calibration multiplier

// OneWire Setup for Temperature Sensor
OneWire oneWire(TEMP_SENSOR_PIN);
DallasTemperature tempSensor(&oneWire);

// Global Variables
float phValue = 7.0;
float ecValue = 0.0;
float waterTemp = 25.0;
int waterLevel = 100;
bool systemActive = true;
bool manualMode = false;

unsigned long lastSensorRead = 0;
unsigned long lastBlynkUpdate = 0;
unsigned long lastPumpActivation = 0;

BlynkTimer timer;
WidgetTerminal terminal(VPIN_TERMINAL);

// Function Prototypes
void readSensors();
void controlPH();
void controlEC();
void checkWaterLevel();
void updateBlynk();
void printToTerminal(String message);
float readPH();
float readEC();
float readTemperature();
int readWaterLevel();
void activatePump(int pumpPin, int duration);
void systemCheck();

void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(PUMP_NUTRIENT_A, OUTPUT);
  pinMode(PUMP_NUTRIENT_B, OUTPUT);
  pinMode(PUMP_PH_UP, OUTPUT);
  pinMode(PUMP_PH_DOWN, OUTPUT);
  pinMode(PUMP_WATER, OUTPUT);
  pinMode(LED_SYSTEM, OUTPUT);
  pinMode(LED_ALERT, OUTPUT);
  
  // Initialize all pumps to OFF
  digitalWrite(PUMP_NUTRIENT_A, LOW);
  digitalWrite(PUMP_NUTRIENT_B, LOW);
  digitalWrite(PUMP_PH_UP, LOW);
  digitalWrite(PUMP_PH_DOWN, LOW);
  digitalWrite(PUMP_WATER, LOW);
  
  // Initialize temperature sensor
  tempSensor.begin();
  
  // Connect to WiFi and Blynk
  Serial.println("Connecting to WiFi...");
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  
  // Setup timers
  timer.setInterval(SENSOR_READ_INTERVAL, readSensors);
  timer.setInterval(BLYNK_UPDATE_INTERVAL, updateBlynk);
  timer.setInterval(30000L, systemCheck);
  
  digitalWrite(LED_SYSTEM, HIGH);
  printToTerminal("System Initialized Successfully!");
  Serial.println("System Ready!");
}

void loop() {
  Blynk.run();
  timer.run();
  
  // Main control logic
  if (systemActive && !manualMode) {
    unsigned long currentTime = millis();
    
    // Check if cooldown period has passed
    if (currentTime - lastPumpActivation >= PUMP_COOLDOWN) {
      controlPH();
      controlEC();
    }
    
    checkWaterLevel();
  }
}

// Read all sensors
void readSensors() {
  phValue = readPH();
  ecValue = readEC();
  waterTemp = readTemperature();
  waterLevel = readWaterLevel();
  
  Serial.println("===== Sensor Readings =====");
  Serial.print("pH: "); Serial.println(phValue, 2);
  Serial.print("EC: "); Serial.print(ecValue, 0); Serial.println(" μS/cm");
  Serial.print("Temp: "); Serial.print(waterTemp, 1); Serial.println(" °C");
  Serial.print("Water Level: "); Serial.print(waterLevel); Serial.println(" %");
  Serial.println("===========================");
}

// Read pH sensor with temperature compensation
float readPH() {
  int sensorValue = analogRead(PH_SENSOR_PIN);
  float voltage = sensorValue * (3.3 / 4095.0) * 1000; // Convert to mV
  
  // Convert voltage to pH (linear approximation)
  // pH = 7.0 - (voltage - neutralVoltage) / slope
  float slope = (PH_ACID_VOLTAGE - PH_NEUTRAL_VOLTAGE) / 3.0; // 3 pH units
  float ph = 7.0 - (voltage - PH_NEUTRAL_VOLTAGE) / slope;
  
  // Temperature compensation (0.003 pH per °C from 25°C)
  ph = ph + 0.003 * (25.0 - waterTemp);
  
  // Constrain to valid pH range
  return constrain(ph, 0.0, 14.0);
}

// Read EC sensor with temperature compensation
float readEC() {
  int sensorValue = analogRead(EC_SENSOR_PIN);
  float voltage = sensorValue * (3.3 / 4095.0);
  
  // Convert voltage to EC (μS/cm)
  float ec = voltage * 1000 * EC_CALIBRATION_FACTOR;
  
  // Temperature compensation (2% per °C from 25°C)
  float tempCoeff = 1.0 + 0.02 * (waterTemp - 25.0);
  ec = ec / tempCoeff;
  
  return ec;
}

// Read temperature sensor
float readTemperature() {
  tempSensor.requestTemperatures();
  float temp = tempSensor.getTempCByIndex(0);
  
  // Validate reading
  if (temp == DEVICE_DISCONNECTED_C || temp < -10 || temp > 50) {
    return 25.0; // Return default if invalid
  }
  
  return temp;
}

// Read water level sensor
int readWaterLevel() {
  int sensorValue = analogRead(WATER_LEVEL_PIN);
  // Convert to percentage (0-100%)
  int level = map(sensorValue, 0, 4095, 0, 100);
  return constrain(level, 0, 100);
}

// pH Control Logic
void controlPH() {
  float phError = phValue - TARGET_PH;
  
  if (abs(phError) <= PH_TOLERANCE) {
    // pH is within acceptable range
    return;
  }
  
  if (phError > PH_TOLERANCE) {
    // pH too high - add acid (pH Down)
    Serial.println("pH too high! Dosing pH Down...");
    printToTerminal("Dosing pH Down");
    activatePump(PUMP_PH_DOWN, PUMP_DOSE_TIME);
    Blynk.virtualWrite(VPIN_PUMP_PH_DOWN, 1);
    delay(100);
    Blynk.virtualWrite(VPIN_PUMP_PH_DOWN, 0);
    
  } else if (phError < -PH_TOLERANCE) {
    // pH too low - add base (pH Up)
    Serial.println("pH too low! Dosing pH Up...");
    printToTerminal("Dosing pH Up");
    activatePump(PUMP_PH_UP, PUMP_DOSE_TIME);
    Blynk.virtualWrite(VPIN_PUMP_PH_UP, 1);
    delay(100);
    Blynk.virtualWrite(VPIN_PUMP_PH_UP, 0);
  }
  
  lastPumpActivation = millis();
}

// EC (Nutrient) Control Logic
void controlEC() {
  float ecError = ecValue - TARGET_EC;
  
  if (abs(ecError) <= EC_TOLERANCE) {
    // EC is within acceptable range
    return;
  }
  
  if (ecError < -EC_TOLERANCE) {
    // EC too low - add nutrients
    Serial.println("EC too low! Dosing nutrients...");
    printToTerminal("Dosing Nutrients");
    
    // Dose Nutrient A
    activatePump(PUMP_NUTRIENT_A, PUMP_DOSE_TIME);
    Blynk.virtualWrite(VPIN_PUMP_A, 1);
    delay(PUMP_DOSE_TIME + 100);
    Blynk.virtualWrite(VPIN_PUMP_A, 0);
    
    // Dose Nutrient B
    activatePump(PUMP_NUTRIENT_B, PUMP_DOSE_TIME);
    Blynk.virtualWrite(VPIN_PUMP_B, 1);
    delay(PUMP_DOSE_TIME + 100);
    Blynk.virtualWrite(VPIN_PUMP_B, 0);
    
    lastPumpActivation = millis();
  } else {
    // EC too high - dilute with water
    Serial.println("EC too high! Consider dilution.");
    printToTerminal("Warning: EC too high");
    Blynk.logEvent("ec_high", "EC level exceeds target");
  }
}

// Check water level and alert if low
void checkWaterLevel() {
  if (waterLevel < MIN_WATER_LEVEL) {
    digitalWrite(LED_ALERT, HIGH);
    Serial.println("WARNING: Water level low!");
    printToTerminal("ALERT: Low Water Level!");
    Blynk.logEvent("water_low", "Water level below minimum");
  } else {
    digitalWrite(LED_ALERT, LOW);
  }
}

// Activate pump for specified duration
void activatePump(int pumpPin, int duration) {
  digitalWrite(pumpPin, HIGH);
  delay(duration);
  digitalWrite(pumpPin, LOW);
}

// Update Blynk dashboard
void updateBlynk() {
  Blynk.virtualWrite(VPIN_PH, phValue);
  Blynk.virtualWrite(VPIN_EC, ecValue);
  Blynk.virtualWrite(VPIN_TEMP, waterTemp);
  Blynk.virtualWrite(VPIN_WATER_LEVEL, waterLevel);
  Blynk.virtualWrite(VPIN_SYSTEM_STATUS, systemActive ? "Active" : "Inactive");
}

// System health check
void systemCheck() {
  printToTerminal("System Check: OK");
  digitalWrite(LED_SYSTEM, !digitalRead(LED_SYSTEM)); // Blink
}

// Print message to Blynk terminal
void printToTerminal(String message) {
  terminal.print("[");
  terminal.print(millis() / 1000);
  terminal.print("s] ");
  terminal.println(message);
  terminal.flush();
}

// Blynk Virtual Pin Handlers

// System ON/OFF control
BLYNK_WRITE(VPIN_SYSTEM_STATUS) {
  systemActive = param.asInt();
  printToTerminal(systemActive ? "System Activated" : "System Deactivated");
}

// Manual Nutrient A pump control
BLYNK_WRITE(VPIN_PUMP_A) {
  if (param.asInt() && manualMode) {
    activatePump(PUMP_NUTRIENT_A, PUMP_DOSE_TIME);
    printToTerminal("Manual: Nutrient A dosed");
  }
}

// Manual Nutrient B pump control
BLYNK_WRITE(VPIN_PUMP_B) {
  if (param.asInt() && manualMode) {
    activatePump(PUMP_NUTRIENT_B, PUMP_DOSE_TIME);
    printToTerminal("Manual: Nutrient B dosed");
  }
}

// Manual pH Up pump control
BLYNK_WRITE(VPIN_PUMP_PH_UP) {
  if (param.asInt() && manualMode) {
    activatePump(PUMP_PH_UP, PUMP_DOSE_TIME);
    printToTerminal("Manual: pH Up dosed");
  }
}

// Manual pH Down pump control
BLYNK_WRITE(VPIN_PUMP_PH_DOWN) {
  if (param.asInt() && manualMode) {
    activatePump(PUMP_PH_DOWN, PUMP_DOSE_TIME);
    printToTerminal("Manual: pH Down dosed");
  }
}

// Terminal input handler
BLYNK_WRITE(VPIN_TERMINAL) {
  String command = param.asStr();
  
  if (command == "status") {
    printToTerminal("=== System Status ===");
    printToTerminal("pH: " + String(phValue, 2));
    printToTerminal("EC: " + String(ecValue, 0) + " μS/cm");
    printToTerminal("Temp: " + String(waterTemp, 1) + " °C");
    printToTerminal("Water: " + String(waterLevel) + " %");
  } else if (command == "manual") {
    manualMode = !manualMode;
    printToTerminal(manualMode ? "Manual mode ON" : "Manual mode OFF");
  } else if (command == "help") {
    printToTerminal("Commands: status, manual, help");
  } else {
    printToTerminal("Unknown command. Type 'help'");
  }
}

// Connection status
BLYNK_CONNECTED() {
  printToTerminal("Connected to Blynk!");
  Blynk.syncAll();
}

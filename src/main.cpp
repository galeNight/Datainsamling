#include <WiFi.h>
#include <FirebaseESP32.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "SPIFFS.h" 
#include <MFRC522.h>
#include <SPI.h>

//RFID pins
#define RST_PIN 22
#define SS_PIN 21

// IR sensor pins
#define IR_SENSOR_2 5
#define IR_SENSOR 2

// DHT sensor pins
#define DHTPIN 4
#define DHTTYPE DHT11

// DHT sensor object
DHT dht(DHTPIN, DHTTYPE);
MFRC522 rfid(SS_PIN, RST_PIN);

// WiFi credentials
const char* ssid = "IoT_Emb";
const char* password = "98806829";

// Firebase settings for board kan tilgå firebase databasen
#define Firebase_host "https://iotproject-f178a-default-rtdb.europe-west1.firebasedatabase.app/"
#define Firebase_auth "hrGCMLLFrMk8di2VpWfoFmHqPPUB1M0xx1kwkX2v"

// Firebase objects
FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;

// NTP client configuration for timestamps
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// Timestamp for periodic sensor reading
unsigned long previousMillis = 0;
const unsigned long interval = 60000; // 1 minute

// IR sensor variables
int previousIRStateOUT = HIGH;
int movementDetectedOUT = 0;
int previousIRStateIND = HIGH;
int movementDetectedIND = 0;

//IR sensor locks and timers
bool sensor2locked=false;
bool sensor5Tlocked=false;
unsigned long sensor2locktime=0;
unsigned long sensor5Tlocktime=0;
const unsigned long unlockInterval=3000;

// Function to start 
void setup() {
  Serial.begin(115200);
  Serial.println("Starting DHT11 test");

  // Initialize DHT sensor
  dht.begin();

  // Initialize IR sensor
  pinMode(IR_SENSOR, INPUT_PULLUP);
  pinMode(IR_SENSOR_2, INPUT_PULLUP);

  // Initialize RFID 
  SPI.begin();
  rfid.PCD_Init();

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // Initialize NTP
  timeClient.begin();
  timeClient.setTimeOffset(3600); // Adjust for UTC offset if needed

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to initialize SPIFFS");
  }

  // Configure Firebase
  config.host = Firebase_host;
  config.signer.tokens.legacy_token = Firebase_auth;

  Firebase.begin(&config, &auth);

  // Check Firebase connection
  if (Firebase.ready()) {
    Serial.println("Connected to Firebase!");
  } else {
    Serial.println("Failed to connect to Firebase");
  }
}

/// Function to check WiFi connection and reconnect if disconnected
void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.print(".");
    }
    Serial.println("\nWiFi reconnected");
  }
}

// Function to reconnect Firebase if disconnected
void reconnectFirebase() {
  if (!Firebase.ready()) {
    Serial.println("Firebase not ready. Reinitializing...");
    Firebase.begin(&config, &auth);
  }
}

// Function to detect movement IND
bool detectMovementIND(){
  static int previousIRState = HIGH;
  int currentIRState = digitalRead(IR_SENSOR);
  if(currentIRState == LOW && previousIRStateIND == HIGH){
    Serial.println("Movement detected");
    previousIRStateIND = currentIRState;
    return true;
  }else{
    //Serial.println("No movement detected");
  }
  previousIRStateIND = currentIRState;
  return false;
}

// function to detect movement OUT
bool detectMovementOUT(){
  static int previousIRState = HIGH;
  int currentIRState = digitalRead(IR_SENSOR_2);
  if(currentIRState == LOW && previousIRStateOUT == HIGH){
    Serial.println("Movement detected");
    previousIRStateOUT = currentIRState;
    return true;
  }else{
    //Serial.println("No movement detected");
  }
  previousIRStateOUT = currentIRState;
  return false;
}

// Function to detect movement direction
void detectDirection() {
  static bool sensor2Triggered = false; 
  static bool sensor5Triggered = false; 
  static unsigned long sensor2Time = 0;
  static unsigned long sensor5Time = 0;
  
  // Auto-unlock sensors after the unlock interval
  if(sensor2locktime && millis() - sensor2locktime > unlockInterval){
    sensor2locked=false;
    sensor2locktime=0;
    sensor2Time = 0;
    Serial.println("Sensor2 auto-unlocked");
  }
  if(sensor5Time && millis() - sensor5Tlocktime > unlockInterval){
    sensor5Tlocked=false;
    sensor5Tlocktime=0;
    sensor5Time = 0;
    Serial.printf("Sensor5 auto-unlocked");
  }
  // Detect movement on sensor 2 (IR_SENSOR)
  if (!sensor2locked && detectMovementIND()) {
    sensor2Triggered = true;
    sensor2Time = millis(); 
    sensor2locked = true;
    sensor2locktime = millis();
    Serial.println("Sensor 2 triggered");
  }
  // Detect movement on sensor 5 (IR_SENSOR_2)
  if (!sensor5Tlocked && detectMovementOUT()) {
    sensor5Triggered = true;
    sensor5Time = millis();
    sensor5Tlocked = true;
    sensor5Tlocktime = millis();
    Serial.println("Sensor 5 triggered");
  }
  // Determine direction based on the sequence of triggers
  if (sensor2Triggered && sensor5Triggered) {
    if (sensor2Time < sensor5Time) {
      Serial.println("Going IN");
      movementDetectedIND++; 
    } else {
      Serial.println("Going OUT");
      movementDetectedOUT++;
    }
    // Reset both triggers
    sensor2Triggered = false;
    sensor5Triggered = false;
    // Unlock both sensors
    sensor2locked = false;
    sensor5Tlocked = false;
  }
}

// Function to save data to SPIFFS cache
void saveToCache(String jsonData) {
  File file = SPIFFS.open("/cache.txt", FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open cache file");
    return;
  }
  file.println(jsonData); // Append the JSON data to the file
  file.close();
  Serial.println("Data saved to cache");
}

// function to read RFID
void readRFID(){
  if(!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()){
    return;
  }
  //read rfid tag uid
  String rfidUID = "";
  for(byte i=0;i<rfid.uid.size;i++){
    rfidUID+=String(rfid.uid.uidByte[i],HEX);
  }
  rfidUID.toUpperCase();
  Serial.print("RFID UID: ");
  Serial.println(rfidUID);

  // get current date and time
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  struct tm *timeInfo = localtime((time_t *)&epochTime);

  char datetime[20];
  sprintf(datetime, "%04d-%02d-%02d %02d:%02d:%02d", 
          timeInfo->tm_year + 1900, 
          timeInfo->tm_mon + 1, 
          timeInfo->tm_mday, 
          timeInfo->tm_hour, 
          timeInfo->tm_min, 
          timeInfo->tm_sec);


  // Send RFID UID to firebase via json object
  FirebaseJson json;
  json.set("rfid", rfidUID);
  json.set("timestamp", datetime);

  // Convert JSON to a string
  String jsonData;
  json.toString(jsonData, true);

  // Attempt to send data to Firebase
  if(WiFi.status() == WL_CONNECTED){
    if(Firebase.pushJSON(firebaseData, "/RFIDData", json)){
      Serial.println("RFID sent to Firebase");
    }else{
      Serial.print("Failed to send RFID: ");
      Serial.println(firebaseData.errorReason());
    }
  }else{
    Serial.println("WiFi disconnected. Saving RFID to cache");
    saveToCache(jsonData); // Save to cache if WiFi is disconnected
  }
  rfid.PICC_HaltA();
}

// Function to upload cached data to Firebase
void uploadCachedData() {
  if (!SPIFFS.exists("/cache.txt")) {
    return;
  }

  // Open the cache file for reading
  File file = SPIFFS.open("/cache.txt", FILE_READ);
  if (!file || !file.available()) {
    file.close();
    return;
  }

  // Read each line from the cache file and send to Firebase
  while (file.available()) {
    String jsonData = file.readStringUntil('\n');
    FirebaseJson json;
    json.setJsonData(jsonData);

    // Determine if data is RFID or SensorData based on content
    if (jsonData.indexOf("\"rfid\"") != -1) {
      if (Firebase.pushJSON(firebaseData, "/RFIDData", json)) {
        Serial.println("Cached RFID data sent to Firebase");
      } else {
        Serial.print("Failed to send cached RFID data: ");
        Serial.println(firebaseData.errorReason());
        break;
      }
    } else {
      if (Firebase.pushJSON(firebaseData, "/SensorData", json)) {
        Serial.println("Cached sensor data sent to Firebase");
      } else {
        Serial.print("Failed to send cached sensor data: ");
        Serial.println(firebaseData.errorReason());
        break;
      }
    }
  }

  file.close();

  // Clear cache if all data was successfully uploaded
  if (WiFi.status() == WL_CONNECTED) {
    SPIFFS.remove("/cache.txt");
    Serial.println("Cache cleared after successful upload");
  }
}

// Main loop
void loop() {
  checkWiFiConnection();
  reconnectFirebase();

  // Check if there's cached data to upload
  if (WiFi.status() == WL_CONNECTED) {
    uploadCachedData(); // Upload cached data when WiFi is reconnected
  }

  // call detectDirection function
  detectDirection();

  // call readRFID function
  readRFID();

    unsigned long currentMillis = millis();

  // Check if 1 minute has passed since the last reading
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // Read temperature and humidity
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("Failed to read from DHT sensor");
      return;
    }

    // Get current timestamp
    timeClient.update();
    unsigned long epochTime = timeClient.getEpochTime();
    struct tm *timeInfo = localtime((time_t *)&epochTime);

    char datetime[20];
    sprintf(datetime, "%04d-%02d-%02d %02d:%02d:%02d", 
            timeInfo->tm_year + 1900, 
            timeInfo->tm_mon + 1, 
            timeInfo->tm_mday, 
            timeInfo->tm_hour, 
            timeInfo->tm_min, 
            timeInfo->tm_sec);


    // Create JSON object
    FirebaseJson json;
    json.set("temperature", temperature);
    json.set("humidity", humidity);
    json.set("timestamp", datetime);
    json.set("movementInd", movementDetectedIND);
    json.set("movementOut", movementDetectedOUT);

    // Convert JSON to a string
    String jsonData;
    json.toString(jsonData, true);

    // Attempt to send data to Firebase
    if (WiFi.status() == WL_CONNECTED) {
      if (Firebase.pushJSON(firebaseData, "/SensorData", json)) {
        Serial.println("Data sent to Firebase");
        movementDetectedIND = 0;
        movementDetectedOUT = 0;
        Serial.println("Resetting both movement counter to: 0");
      } else {
        Serial.print("Failed to send data. Saving to cache: ");
        Serial.println(firebaseData.errorReason());
        saveToCache(jsonData); // Save to cache if Firebase operation fails
      }
    } else {
      Serial.println("WiFi disconnected. Saving to cache");
      saveToCache(jsonData); // Save to cache if WiFi is disconnected
    }
  }
}

#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include "HX711.h"
#include "soc/rtc.h"
#include <Keypad.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include "driver/gpio.h"
#include <ArduinoJson.h>

//Keypad Configuration
const byte ROWS = 4; // Four rows
const byte COLS = 4; // Four columns
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {13, 12, 14, 27}; // Connect to the row pinouts of the keypad
byte colPins[COLS] = {26, 25, 33, 32}; // Connect to the column pinouts of the keypad
Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );
bool inputStarted = false;
bool readyCheck =  false;
bool inputGiven =  false;
String inputString = "";
float inputNumber = 0.0;
// Array to map each number button to its corresponding letters
const char* letters[] = {
  "0", "1", "2abc", "3def", "4ghi", "5jkl", "6mno", "7pqrs", "8tuv", "9wxyz"
};
int buttonPresses[10] = {0};
String outputText[20];
int currentIndex = 0;

float currentHead = 0.0;
String liveInput = "";

// Loadcell Configuration
#define LOADCELL_DOUT_PIN 16
#define LOADCELL_SCK_PIN 17
HX711 scale;
float currentWeight = 0.0;

// RFID Reader Configuration
#define SS_PIN  5
#define RST_PIN 4
MFRC522 rfid(SS_PIN, RST_PIN);
String uid = "";

//Ultrasonic Sensor Configuration
#define TRIG_PIN 21
#define ECHO_PIN 22
float duration_us, distance_cm;
float currentHeight = 0.0;

//LCD TFT Configuration
#define TFT_CS         2
#define TFT_RST        3                                            
#define TFT_DC         15
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
String displayNama = "";
String result = "";

// Firebase Configuration
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseJson content;

#define FIREBASE_HOST "stuntingapp-d423a"
#define API_KEY "AIzaSyB9yOXr1xhwfAWT-NAoWhkGMMjlB9fjxZI"
#define USER_EMAIL "test@gmail.com"
#define USER_PASSWORD "Test123"

unsigned long sendDataPrevMillis = 0;
bool isUserRegistered = false;

void setup() {
  Serial.begin(115200);
  disableLED();
  tft.initR(INITR_BLACKTAB);
  uint16_t time = millis();
  tft.fillScreen(ST77XX_BLACK);
  time = millis() - time;
  Serial.println(time, DEC);
  delay(500);
  tft.setRotation(2);

  //Ultrasonic sensor init
  pinMode(ECHO_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);

  //Loadcell sensor init
  Serial.println("Initializing the scale");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  Serial.println("Before setting up the scale:");
  Serial.print("read: \t\t");
  Serial.println(scale.read());      // print a raw reading from the ADC
  Serial.print("read average: \t\t");
  Serial.println(scale.read_average(20));   // print the average of 20 readings from the ADC
  Serial.print("get value: \t\t");
  Serial.println(scale.get_value(5));   // print the average of 5 readings from the ADC minus the tare weight (not set yet)
  Serial.print("get units: \t\t");
  Serial.println(scale.get_units(5), 1);  // print the average of 5 readings from the ADC minus tare weight (not set) divided
  scale.set_scale(105.12);
  scale.tare();               // reset the scale to 0
  Serial.println("After setting up the scale:");
  Serial.print("read: \t\t");
  Serial.println(scale.read());                 // print a raw reading from the ADC
  Serial.print("read average: \t\t");
  Serial.println(scale.read_average(20));       // print the average of 20 readings from the ADC
  Serial.print("get value: \t\t");
  Serial.println(scale.get_value(5));   // print the average of 5 readings from the ADC minus the tare weight, set with tare()
  Serial.print("get units: \t\t");
  Serial.println(scale.get_units(5), 1);        // print the average of 5 readings from the ADC minus tare weight, divided
  Serial.println("Readings:");

  // RFID Reader Init
  SPI.begin(); // init SPI bus
  rfid.PCD_Init(); // init MFRC522
  Serial.println("Tap an ID card");

  // Connect to WiFi
  tft.setCursor(3,10);
  WiFi.begin("ID HOME", "abcdefgh");
  tft.fillScreen(ST77XX_BLACK);
  tft.println("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    tft.print(".");
  }
  tft.setCursor(3,10);
  tft.fillScreen(ST77XX_BLACK);
  tft.println("Connected to WiFi");
  tft.println("Connected with IP: ");
  tft.print(WiFi. localIP());

  // Firebase Init
  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  // Assign the callback function for the long-running token generation task
  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  // You may need to adjust the buffer sizes and response size depending on your requirements
    // Since v4.4.x, BearSSL engine was used, the SSL buffer need to be set.
    // Large data transmission may require larger RX buffer, otherwise connection issue or data read time out can be occurred.
    fbdo.setBSSLBufferSize(4096 /* Rx buffer size in bytes from 512 - 16384 */, 1024 /* Tx buffer size in bytes from 512 - 16384 */);
    // Limit the size of response payload to be collected in FirebaseData
    fbdo.setResponseSize(2048);
}

void loop() {
  char key = keypad.getKey();
  if (key && !inputStarted) {
    tft.fillScreen(ST77XX_BLACK);
    drawText("Input Baby UID", ST77XX_WHITE, 3, 5, 1);
    inputStarted = true;
  } else if (!inputStarted){
    tft.fillScreen(ST77XX_BLACK);
    drawText("Scan ID Card or Press Any Key to Start", ST77XX_WHITE, 3, 5, 1);
  }
  if (rfid.PICC_IsNewCardPresent()) {
    readNFC();
    if (Firebase.ready() && readyCheck) {
      delay(500);
      checkDocumentExistence(uid);
    }
  } else if (inputStarted){
    tft.fillScreen(ST77XX_BLACK);
    drawText("Input Baby UID", ST77XX_WHITE, 3, 5, 1);
    if (Firebase.ready() && readyCheck){
      delay(500);
      checkDocumentExistence(uid);
    } else if (!readyCheck) {
      inputUID(key);
      char liveInputBuffer[liveInput.length() + 1];
      liveInput.toCharArray(liveInputBuffer, liveInput.length() + 1);
      drawText(liveInputBuffer, ST77XX_BLUE, 3, 35, 3);
    }
  }

  if (!isUserRegistered && uid ==  " ") {
    tft.fillScreen(ST77XX_BLACK);
    drawText("...", ST77XX_WHITE, 3, 5, 1);
    Serial.print(".");
    delay(5000);
    readyCheck = false;
    inputStarted = false;
    uid = "empty";
    currentWeight = 0.00;
    currentHeight = 0.00;
    currentHead = 0.00;
  } else if (isUserRegistered){
    tft.fillScreen(ST77XX_BLACK);
    String documentPath = "Anak/" + uid;
    String collectionPath = "Anak";
    fetchData(collectionPath, uid);
    char displayNamaBuffer[displayNama.length() + 1];
    displayNama.toCharArray(displayNamaBuffer, displayNama.length() + 1); 
    drawText("Hello!" , ST77XX_WHITE, 3, 5, 2);
    drawText(displayNamaBuffer , ST77XX_WHITE, 3, 50, 3);
    delay(3000);
    while (currentWeight <= 0.1) {
      tftPrint("Berat Badan:", currentWeight, "kg");
      measureWeight();
    }
    Serial.print(currentWeight);
    tftPrint("Berat Badan:", currentWeight, "kg");
    delay(2000);
    inputNumber = 0.00;
    tft.fillScreen(ST77XX_BLACK);
    while (inputGiven == false) {
      char weightManual = keypad.getKey();
      Serial.println(weightManual);
      if (weightManual != NO_KEY) {
        if (weightManual == '#') {
          if (!liveInput.isEmpty()) {
            inputNumber = liveInput.toFloat(); // Update inputNumber from liveInput
            currentWeight = inputNumber; // Update currentWeight
          }
          inputNumber = 0.00;
          inputString = "";
          liveInput = "";
          inputGiven = true;
        } else {
          inputManual(weightManual);
          float manualWeight = liveInput.toFloat();
          tftPrint("Berat Badan:", manualWeight, "kg");
        }
      }
      drawText("# to continue, input weight to edit", ST77XX_WHITE, 3, 100, 1);
    }
    tftPrint("Berat Badan:", currentWeight, "kg");
    delay(2000);
    tft.fillScreen(ST77XX_BLACK);
    inputGiven = false;
    while (currentHeight < 20) {
      measureHeight();
      tftPrint("Tinggi Badan:", currentHeight, "cm");
    }
    tftPrint("Tinggi Badan:", currentHeight, "cm");
    delay(2000);
    inputNumber = 0.00;
    tft.fillScreen(ST77XX_BLACK);
    while (inputGiven == false) {
      char heightManual = keypad.getKey();
      Serial.println(heightManual);
      if (heightManual != NO_KEY) {
        if (heightManual == '#') {
          if (!liveInput.isEmpty()) {
            inputNumber = liveInput.toFloat(); // Update inputNumber from liveInput
            currentHeight = inputNumber; // Update currentHeight
          }
          inputNumber = 0.00;
          inputString = "";
          liveInput = "";
          inputGiven = true;
        } else {
          inputManual(heightManual);
          float manualHeight = liveInput.toFloat();
          tftPrint("Tinggi Badan:", manualHeight, "cm");
        }
      }
      drawText("# to continue, input height to edit", ST77XX_WHITE, 3, 100, 1);
    }
    tftPrint("Tinggi Badan:", currentHeight, "cm");
    delay(2000);
    inputGiven = false;
    tft.fillScreen(ST77XX_BLACK);
    Serial.print("enter head circumference");
    while (currentHead == 0.00) {
      inputNumber = 0.00;
      float Head = liveInput.toFloat();
      tftPrint("Lingkar Kepala:", Head, "cm");
      drawText("Masukkan Lingkar Kepala Bayi", ST77XX_WHITE, 3, 100, 1);
      Serial.println(inputNumber);
      char keyManual = keypad.getKey();
      inputManual(keyManual);
      currentHead = inputNumber;
      
    }
    tftPrint("Lingkar Kepala:", currentHead, "cm");
    while (keypad.getKey() != '#') {
      delay(500);
      drawText("# to finish", ST77XX_WHITE, 3, 100, 1);
    }
    printSummary();

    updateData(documentPath);
    delay(3000);
    tft.fillScreen(ST77XX_BLACK);
    drawText("Calculating...", ST77XX_WHITE, 3, 5, 2);
    delay(3000);
    tft.fillScreen(ST77XX_BLACK);
    fetchData(collectionPath, uid);
    char resultBuffer[result.length() + 1];
    result.toCharArray(resultBuffer, result.length() + 1); 
    drawText(resultBuffer, ST77XX_WHITE, 3, 5, 2);
    delay(5000);
    uid = " ";
    isUserRegistered = false;
  }
}

void inputUID(char key){
  if (key == '#') {
    liveInput = "";
    readyCheck = true;
    uid = "";
    for (int i = 0; i <= currentIndex; i++) {
      uid += outputText[i];
    }
    Serial.println(uid);
    currentIndex = 0;
    for (int i = 0; i < 10; i++) {
      outputText[i] = "";
    }
    memset(buttonPresses, 0, sizeof(buttonPresses));
  } else if (key != NO_KEY) {
    unsigned long currentTime = millis();
    static char previousButton = 0;
    static unsigned long lastKeyPressTime = 0;
    
    if (key == previousButton && (currentTime - lastKeyPressTime) < 1500) {
    int buttonIndex = -1;
    switch (key) {
      case '0': buttonIndex = 0; break;
      case '1': buttonIndex = 1; break;
      case '2': buttonIndex = 2; break;
      case '3': buttonIndex = 3; break;
      case '4': buttonIndex = 4; break;
      case '5': buttonIndex = 5; break;
      case '6': buttonIndex = 6; break;
      case '7': buttonIndex = 7; break;
      case '8': buttonIndex = 8; break;
      case '9': buttonIndex = 9; break;
      default: break;
    }
      buttonPresses[buttonIndex]++;
      int letterIndex = buttonPresses[buttonIndex] % strlen(letters[buttonIndex]);
      char letter = letters[buttonIndex][letterIndex];
      outputText[currentIndex] = letter;
      liveInput = "";
      for (int i = 0; i <= currentIndex; i++) {
      liveInput += outputText[i];
      }
      // Update last key press time
      lastKeyPressTime = currentTime;
    } else if (key == 'D') {
      // Delete the latest character entered if 'D' is pressed
      if (currentIndex >= 0) {
        outputText[currentIndex] = ""; // Clear the character at this position
        currentIndex--; // Move back one position
        if (currentIndex < 0) {
            currentIndex = 0; // Ensure currentIndex doesn't go negative
        }
      }
      if (!liveInput.isEmpty()) {
          liveInput.remove(liveInput.length() - 1);
        }
    } else {
      currentIndex++;
      if (currentIndex >= 20) {
        currentIndex = 0;
      }
      if (currentIndex != 0) {
        for (int i = 0; i < 10; i++) {
            buttonPresses[i] = 0;
        }
        liveInput = "";
        for (int i = 0; i <= currentIndex; i++) {
        liveInput += outputText[i];
      }
    }
      int buttonIndex = key - '0';
      outputText[currentIndex] = letters[buttonIndex][0];
      liveInput = "";
      for (int i = 0; i <= currentIndex; i++) {
        liveInput += outputText[i];
      }
      previousButton = key;
      lastKeyPressTime = currentTime;
    }
  }
}

void readNFC(){
  uid = "";
    if (rfid.PICC_ReadCardSerial()) { 
      MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
      Serial.print("RFID/NFC Tag Type: ");
      Serial.println(rfid.PICC_GetTypeName(piccType));
      // Convert UID to hexadecimal string format without spaces
      for (byte i = 0; i < rfid.uid.size; i++) {
        uid += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
        uid += String(rfid.uid.uidByte[i], HEX);
      }
      Serial.print("UID: ");
      Serial.println(uid);
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      readyCheck = true;
    }
}

void checkDocumentExistence(String uid) {
  String path = "Anak/" + uid;
  // Check if child is registered on the database
  if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_HOST, "", path.c_str())) {
    Serial.println("Registered");
    isUserRegistered = true;
    currentWeight = 0.0;
    currentHeight = 0.0;
  }
  else {
    Serial.println("Unregistered");
    isUserRegistered = false;
    Serial.println(fbdo.errorReason());
    readyCheck = false;
  }
}


void updateData(String path) {
    content.set("fields/bb/doubleValue", float(currentWeight));
    content.set("fields/tb/doubleValue", float(currentHeight));
    content.set("fields/lk/doubleValue", float(currentHead));

    if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_HOST, "", path.c_str(), content.raw(), "bb") && Firebase.Firestore.patchDocument(&fbdo, FIREBASE_HOST, "", path.c_str(), content.raw(), "tb") && Firebase.Firestore.patchDocument(&fbdo, FIREBASE_HOST, "", path.c_str(), content.raw(), "lk")) {
      Serial.printf("ok\n%s\n\n", fbdo.payload().c_str());
    } else {
      Serial.println(fbdo.errorReason());
    }
}

void fetchData(String collection, String documentId) {
    String path = collection + "/" + documentId;
    Serial.print("Fetching data from document: ");
    Serial.println(path);

    if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_HOST, "", path.c_str(), "")) {
        Serial.println("Data retrieval successful");
        StaticJsonDocument<1024> doc;
        DeserializationError error = deserializeJson(doc, fbdo.payload().c_str());

        if (!error) {
            if (doc.containsKey("fields")) {
                JsonObject fields = doc["fields"];

                if (fields.containsKey("nama")) {
                    JsonObject namaObject = fields["nama"];

                    if (namaObject.containsKey("stringValue")) {
                        String namaValue = namaObject["stringValue"].as<String>();
                        displayNama = namaValue;
                        Serial.print("Nama value: ");
                        Serial.println(namaValue);
                    }
                }
                if (fields.containsKey("hasil")) {
                    JsonObject hasilObject = fields["hasil"];

                    if (hasilObject.containsKey("stringValue")) {
                        String hasilValue = hasilObject["stringValue"].as<String>();
                        result = hasilValue;
                        Serial.print("Hasil value: ");
                        Serial.println(hasilValue);
                    }
                }
            }
        }
    } else {
        Serial.print("Failed to retrieve data from document: ");
        Serial.println(path);
        Serial.println(fbdo.errorReason());
    }
}

void measureWeight() {
  Serial.print("One reading:\t");
  Serial.print(scale.get_units(), 1);
  Serial.print("\t| Average:\t");
  Serial.println(scale.get_units(10), 5);
  float unitConverter = scale.get_units() / 1000.00;
  currentWeight = unitConverter;
  delay(3000);
}

void measureHeight() {
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  duration_us = pulseIn(ECHO_PIN, HIGH);
  // Calculate the distance
  distance_cm = 0.017 * duration_us;
  Serial.print("Distance: ");
  Serial.print(distance_cm);
  Serial.println(" cm");
  currentHeight = distance_cm;
  delay(500);
}

void inputManual(char key) {
  if (key != NO_KEY) {
    if (key == '#') {
      if (inputString.length() > 0) {
        inputNumber = inputString.toFloat();
        Serial.print("Input number: ");
        Serial.println(inputNumber, 2); // Print with 2 decimal places
        inputString = ""; // Clear the input string for next input
      }
      liveInput = "";
    } else if (key == '*') {
      inputString += '.';
      liveInput += '.';
    } else {
      inputString += key;
      liveInput += key;
    }
  }
}

void drawText(char *text, uint16_t color, int x, int y, int size) {
  tft.setCursor(x, y);
  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.setTextWrap(true);
  tft.print(text);
}

void tftPrint(String text, float data, String unit) {
  tft.setCursor(3, 35);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.println(text);
  tft.setCursor(3, 75);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_GREEN);
  tft.println(String(data) + unit);
}

void printSummary() {
  uint16_t titleColor = ST77XX_WHITE;
  uint16_t bodyColor = ST77XX_GREEN;
  tft.setCursor(3, 5);
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(titleColor);
  tft.setTextSize(1);
  tft.println("Data Summary");
  tft.setTextColor(titleColor);
  tft.setCursor(3, 30);
  tft.setTextSize(1);
  tft.println("Berat Badan:");
  tft.setCursor(3, 40);
  tft.setTextSize(1);
  tft.setTextColor(bodyColor);
  tft.println(String(currentWeight) + "kg");
  tft.setCursor(3, 60);
  tft.setTextColor(titleColor);
  tft.setTextSize(1);
  tft.println("Tinggi Badan:");
  tft.setTextSize(1);
  tft.setCursor(3, 70);
  tft.setTextColor(bodyColor);
  tft.println(String(currentHeight) + "cm");
  tft.setCursor(3, 90);
  tft.setTextColor(titleColor);
  tft.setTextSize(1);
  tft.println("Lingkar Kepala:");
  tft.setTextSize(1);
  tft.setCursor(3, 100);
  tft.setTextColor(bodyColor);
  tft.println(String(currentHead) + "cm");
}

void disableLED() {
  gpio_pad_select_gpio(GPIO_NUM_2); // Select GPIO 2
  gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT); // Set it as an output pin
  gpio_set_level(GPIO_NUM_2, 0); // Turn off the LED
}

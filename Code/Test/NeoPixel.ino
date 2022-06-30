// Libraries
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <FastLED.h>
#include <bsec.h>
#include <hp_BH1750.h>

#ifdef __AVR__
  #include <avr/power.h>
#endif

// ----------------------------------------------------------------------------

// ID
#define ID 1

// WiFi
const char *ssid = "HTC U11";
const char *password = "a28139348";

// MQTT
const char *mqtt_server = "mqtt.iot.informatik.uni-oldenburg.de";
const int mqtt_port = 2883;
const char *mqtt_user = "sutk";
const char *mqtt_pw = "SoftSkills";

// ----------------------------------------------------------------------------

// PINOUT
#define STRIP1 14 // D5
#define NUM1 24

#define STRIP2 12 // D6
#define NUM2 8

int button = 13; // D7

// NeoPixel
Adafruit_NeoPixel strip1 = Adafruit_NeoPixel(NUM1, STRIP1, NEO_GRBW + NEO_KHZ800);
// FastLED
CRGB strip2[NUM2];
CRGB lastLED[NUM2]; // Second Array to copy last LED state

// BH1750
hp_BH1750 BH1750;

// BME680
Bsec BME680;

// Network
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
WiFiClient espClient;
PubSubClient client(espClient);

// Variables
// Timer
unsigned int timerWork = 25;
unsigned int timerBreak = 5;
boolean pomodoro = false; // Timer inactive
unsigned long startTime = 0;
unsigned int sinceBoot = 0; // Completed timer since boot
unsigned int inRow = 0; // Completed timer in a Row
// Light
boolean light = true; // LEDs on
unsigned int brightness = 125;
unsigned int num = 0; // global variable to move red light in circle through setup
boolean automatic = true; // Brightness automatic instead of MQTT
// Clock
unsigned int minute; // Current minute
unsigned int minuteLED; // LED corresponding to the current minute
unsigned int hour; // Current hour
unsigned int hourLED; // LED corresponding to the current hour
unsigned long last = 0; // Timestamp of last calculation of minute and hour
// MQTT
String id = String("cloc-") + String(ID); // ID 
String topic; // Topic String to convert to char*
char msg[8]; // Char Array to hold mqtt messages
// Button
unsigned int lastState = 0; // Last Button State
// Lux
float lastLux = 0;
float lastLuxSend = 0;
unsigned long lastBrightness = 0;

// Setup
void setup() {
  Serial.begin(115200);
  Wire.begin(); // Enable i2c
  
  pinMode(button, INPUT); // Button pin input
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // Turn build-in LED off
  
  strip1.begin();
  strip1.clear(); // Initialize all pixels to 'off'
  strip1.show();

  FastLED.addLeds<WS2812B, STRIP2, GRB>(strip2, NUM2);
  FastLED.clear(); // Initialize all pixels to 'off'
  FastLED.show();

  // Initialize BME680
  if (BH1750.begin(BH1750_TO_GROUND)) { // check for BME680
    BH1750.start();
  } else {
    Serial.println("BH1750 NOT FOUND");
  }
  BME680.begin(BME680_I2C_ADDR_PRIMARY, Wire);
  if (BME680.status != BSEC_OK || BME680.bme680Status != BME680_OK) {
    Serial.print("FAIL ");
    Serial.print(BME680.status);
    Serial.print(" : ");
    Serial.println(BME680.bme680Status);
  }
  bsec_virtual_sensor_t sensorList[10] =
  {
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
  };
  BME680.updateSubscription(sensorList, 10, BSEC_SAMPLE_RATE_LP);

  setup_wifi();
  timeClient.begin(); // NTP Time
  timeClient.setTimeOffset(7200);
  client.setServer(mqtt_server, mqtt_port); // MQTT
  client.setCallback(callback);

  getTime();
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (num >= strip1.numPixels()) {
      num = 0;
    }
    strip1.clear();
    strip1.setPixelColor(num, strip1.Color(brightness, 0, 0, 0));
    strip1.show();
    num++;
  }
  randomSeed(micros());
  Serial.println();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
      Serial.print("Connecting to ");
    Serial.println(mqtt_server);
  while (!client.connected()) {
    if (client.connect(id.c_str(), mqtt_user, mqtt_pw)) {
      Serial.print("connected as ");
      Serial.println(id);
      // Subscribe MQTT topics
      topic = id + "/light";
      client.subscribe(topic.c_str());
      topic = id + "/timer";
      client.subscribe(topic.c_str());
      topic = id + "/light/brightness";
      client.subscribe(topic.c_str());
      topic = id + "/timer/duration/work";
      client.subscribe(topic.c_str());
      topic = id + "/timer/duration/break";
      client.subscribe(topic.c_str());

      // Publish default values on reconnect
      topic = id + "/light/status";
      client.publish(topic.c_str(), "ON");
      topic = id + "/timer/status";
      client.publish(topic.c_str(), "OFF");
      sprintf(msg, "%d", sinceBoot);
      topic = id + "/timer/counter/sinceBoot";
      client.publish(topic.c_str(), msg);
      topic = id + "/timer/counter/inRow";
      sprintf(msg, "%d", inRow);
      client.publish(topic.c_str(), msg);
      topic = id + "/timer/duration/work/status";
      sprintf(msg, "%d", timerWork);
      client.publish(topic.c_str(), msg);
      topic = id + "/timer/duration/break/status";
      sprintf(msg, "%d", timerBreak);
      client.publish(topic.c_str(), msg);
      topic = id + "/light/brightness/status";
      client.publish(topic.c_str(), "A");
    } else {
      Serial.print(".");
      if (num >= strip1.numPixels()) {
        num = 0;
      }
      strip1.clear();
      strip1.setPixelColor(num, strip1.Color(brightness, 0, 0, 0));
      strip1.show();
      num++;
      delay(500);
    }
  }
}

// Callback
void callback(char *inTopic, byte *payload, unsigned int length) {
  String newTopic = inTopic;
  payload[length] = '\0';
  String newPayload = String((char *)payload);
  int intPayload = newPayload.toInt();
  Serial.print("Message arrived: ");
  Serial.print(newTopic);
  Serial.print(" ");
  Serial.println(newPayload);
  if (newTopic == id + "/light") {
    if (newPayload == "ON") {
      startLight();
    } else if (newPayload == "OFF") {
      stopLight();
    }
  } else if (newTopic == id + "/timer") {
    if (newPayload == "ON") {
      startTimer();
    } else if (newPayload == "OFF") {
      stopTimer();
    }
  } else if (newTopic == id + "/light/brightness") {
    if (newPayload == "A") {
      automatic = true;
      topic = id + "/light/brightness/status";
      client.publish(topic.c_str(), "A");
    } else {
      automatic = false;
      brightness = map(constrain(intPayload, 8, 255), 0, 100, 8, 255);
      sprintf(msg, "%d", intPayload);
      topic = id + "/light/brightness/status";
      client.publish(topic.c_str(), msg);
    }
  } else if (newTopic == id +"/timer/duration/work") {
    if (intPayload > 0) {
      timerWork = intPayload;
      topic = id + "/timer/duration/work/status";
      sprintf(msg, "%d", timerWork);
      client.publish(topic.c_str(), msg);
    }
  } else if (newTopic == id +"/timer/duration/break") {
    if (intPayload > 0) {
      timerBreak = intPayload;
      topic = id + "/timer/duration/break/status";
      sprintf(msg, "%d", timerBreak);
      client.publish(topic.c_str(), msg);
    }
  }
}

// Light
void startLight() {
  memcpy(strip2, lastLED, sizeof(lastLED)); // Copy last state in current state
  FastLED.show(); 
  light = true;
  Serial.println("LIGHT: ON");
  topic = id + "/light/status";
  client.publish(topic.c_str(), "ON");
}
void stopLight() {
  strip1.clear();
  strip1.show();
  FastLED.clear(true);
  light = false;
  Serial.println("LIGHT: OFF");
  topic = id + "/light/status";
  client.publish(topic.c_str(), "OFF");
}

// Clock
void getTime() {
  if (millis() - last > 30000 || last == 0) { // LAst update > 3000 or just booted
    timeClient.update();
    last = millis();

    hour = timeClient.getHours(); // Get current hour
    Serial.print("Time: ");
    Serial.print(hour);
    if (hour > 12) { // get 12h format
      hour -= 12;
    }
    minute = timeClient.getMinutes(); // Get current minute
    Serial.print(":");
    Serial.print(minute);
    Serial.println();

    hourLED = map(hour, 0, 12, 0, strip1.numPixels() - 1); // Map hour to LED
    minuteLED = map(minute, 0, 60, 0, strip1.numPixels() - 1); // Map minute to LED
  }
  updateClock();
}

void updateClock() {
  if (light) {
    strip1.clear(); // All LEDs off
    if (hourLED != minuteLED) { // Second LED (minute)
      strip1.setPixelColor(minuteLED, strip1.Color(brightness, 0, 0, 0));
    }
    strip1.setPixelColor(hourLED, strip1.Color(0, 0, 0, brightness)); // First LED
    strip1.show();
  }
}

// Timer
void checkButton() {
  int state = digitalRead(button);
  if (state != lastState) { // State changed
    lastState = state;
    if (state == HIGH) { // Button pressed
      if (pomodoro) { // Timer on
        stopTimer();
      } else {
        startTimer();
      }
    }
  }
}

void startTimer() {
  pomodoro = true;
  startTime = millis();
  Serial.println("TIMER: ON");
  topic = id + "/timer/status";
  client.publish(topic.c_str(), "ON");
  if (sinceBoot > 0) {
    topic = id + "/timer/counter/sinceBoot";
    sprintf(msg, "%d", sinceBoot);
    client.publish(topic.c_str(), msg);
  }
  if (inRow == 0) {
    topic = id + "/timer/counter/inRow";
    sprintf(msg, "%d", inRow);
    client.publish(topic.c_str() , msg);
  } else if (inRow > 0) {
    topic = id + "/timer/counter/inRow";
    sprintf(msg, "%d", inRow);
    client.publish(topic.c_str() , msg);
  }
}

void stopTimer() {
  pomodoro = false;
  inRow = 0;
  Serial.println("TIMER: OFF");
  topic = id + "/timer/status";
  client.publish(topic.c_str() , "OFF");
}

void updateTimer() {
  if (millis() - startTime > 1000 * 60 * timerWork) { // Work time finished
    if (millis() - startTime > 1000 * 60 * (timerWork + timerBreak)) { // Timer finished
      sinceBoot++;
      inRow++; // increment counter
      return startTimer(); // Start new timer
    }
    if (light) {
      // Pause
      int past = map(1000 * 60 * timerBreak - (millis() - (startTime + 1000 * 60 * timerWork)), 0, 1000 * 60 * timerWork, strip1.numPixels() - 1, -1);
      strip1.clear();
      if (past >= 0) {
        for (int i = 0; i <= past; i++) {
           strip1.setPixelColor(strip1.numPixels() - 1 - i, strip1.Color(0, brightness, 0, 0)); // Green
        }
      }
      strip1.show();
    }
  } else {
    if (light) {
       // Arbeiten
       int past = map(1000 * 60 * timerWork - (millis() - startTime), 0, 1000 * 60 * timerWork, -1, strip1.numPixels() - 1);
       strip1.clear();
       if (past >= 0) {
         for (int i = 0; i <= past; i++) {
            strip1.setPixelColor(i, strip1.Color(0, 0, 0, brightness)); // White
         }
       }
       strip1.show();
    }
  }
}

// Sensoren
// BH1750
void bh1750() {
  if (BH1750.hasValue() == true) {
    float lux = BH1750.getLux();
    BH1750.start();
    if (abs(lux - lastLux) > 3 || lastLux == 0) { // Value changed more than 3 or just booted
      lastLux = lux;
      if (automatic) {
        brightness = map(constrain(lux, 8, 255), 0, 150, 8, 255); // Map Lux to brightness scale
      }
    }
    if (millis() - lastBrightness > 5000 || lastBrightness == 0) { // Send lux through MQTT every 5 seconds if value changed more than 3
      if (abs(lux - lastLuxSend) > 3 || lastLuxSend == 0) { 
        lastLuxSend = lux;
        topic = id + "/sens/lux";
        dtostrf(lux, 3, 1, msg); // Convert float to char*
        client.publish(topic.c_str(), msg);
        lastBrightness = millis();
      }
    }
  }
}

// BME680
void bme680() {
  if (BME680.run()) { // new value arrived
    String output = String(BME680.iaq);
    output += ", " + String(BME680.iaqAccuracy);
    output += ", " + String(BME680.temperature);
    output += ", " + String(BME680.humidity);
    output += ", " + String(BME680.staticIaq);
    output += ", " + String(BME680.co2Equivalent);
    output += ", " + String(BME680.breathVocEquivalent);
    Serial.println(output);

    // Temperature
    float temp = BME680.temperature;
    int h = map(constrain(temp, 15, 25), 15, 25, 240, 0); // Map temperature to HSV scale between 240, 0
    if ((h < 160 && h > 80) || !light) { // if light off or h green don't show
      strip2[0] = CRGB::Black;
    } else {
      Serial.println(h);
      strip2[0] = CHSV(h, 255, brightness);
      FastLED.show();
      lastLED[0] = CHSV(h, 255, brightness); // Make copy
    }
    // Humidity
    float hum = BME680.humidity;
    h = map(constrain(hum, 40, 60), 40, 60, 0, 240); // Map humidity to HSV scale between 0, 240
    if ((h < 160 && h > 80) || !light) { // if light off or h green don't show
      strip2[1] = CRGB::Black;
    } else {
      Serial.println(h);
      strip2[1] = CHSV(h, 255, brightness);
      FastLED.show();
      lastLED[1] = CHSV(h, 255, brightness);
    }
    // IAQ
    if (BME680.iaqAccuracy > 0) { // IAQ Accuracy above 0 to get safe values
      
    }
  } else if (BME680.status != BSEC_OK || BME680.bme680Status != BME680_OK) { // Fail
    Serial.print("FAIL ");
    Serial.print(BME680.status);
    Serial.print(" : ");
    Serial.println(BME680.bme680Status);
  }
}

// Loop
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  checkButton();
  bh1750();
  bme680();
  if (pomodoro) { // Timer
    updateTimer();
  } else {
    getTime();
  }
}

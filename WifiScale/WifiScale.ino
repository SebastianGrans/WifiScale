
#include <ESP8266WiFi.h>

// The two following libraries can be downloaded here:
// https://github.com/electronicsguy/ESP8266/tree/master/HTTPSRedirect
#include "HTTPSRedirect.h"
#include "DebugMacros.h"

#include "HX711.h"
#define DOUT  2
#define CLK  3
// You need to change this factor depending on your scale.
float calibration_factor = 25750;  

HX711 scale;

// WIFI Settings
// Fill ssid and password with your network credentials
const char* ssid = "<ssid>"; //  ******* CHANGE HERE **********
const char* password = "<password>"; //  ******* CHANGE HERE **********

const char* host = "script.google.com";
// Replace with your own script id to make server side changes
const char *GScriptId = "<script id>"; //  ******* CHANGE HERE **********
const int httpsPort = 443;

// Write to Google Spreadsheet
String url = String("/macros/s/") + GScriptId + "/exec?value=";
String url2 = String("/macros/s/") + GScriptId + "/exec?";

String payload_base =  "{\"command\": \"appendRow\", \
                    \"sheet_name\": \"<your sheet name>\", \
                    \"values\": "; //  ******* CHANGE HERE **********
                    
String payload = "";

HTTPSRedirect* client = nullptr;

// The scale is basically a state machine.
// State 0: waiting for load on the scale
// if timeout: Goto state 4
// State 1: Stablize (I tested this, and it takes ~10 seconds before it stabalizes somewhat)
// State 2: Sample for x seconds
// TODO: Make State 1 smarter. Implement a feedback look that looks at the variance.
// State 3: Send to spreadsheet
// State 4: Go into deep sleep

int state = 0; 
unsigned long last_time = 0;
unsigned long current_time = 0;
unsigned long state_0_timeout = 10000; // How long to wait for a load before timing out.
unsigned long state_1_wait = 10000; // How long to wait for a load.
double no_load = 0;
double load_threshold = 10; // 10 kilo threshold before going into state 1.
int num_upload_fails = 0;



void setup() {
  
  Serial.begin(9600);
  Serial.flush();

  Serial.println();
  Serial.print("Connecting to wifi: ");
  Serial.println(ssid);
  // flush() is needed to print the above (connecting...) message reliably,
  // in case the wireless connection doesn't go through
  Serial.flush();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  scale.power_up();
  scale.begin(D2, D3);
  scale.set_scale(calibration_factor);
  scale.tare();  //Reset the scale to 0
  no_load = scale.get_units(10);

  last_time = millis(); // Time at boot. 
  DPRINTLN("DEBUG :: SETUP :: last_time: " + String(last_time));

  pinMode(D4, OUTPUT);
  pinMode(D0, OUTPUT);
  digitalWrite(D4, HIGH);
  digitalWrite(D0, HIGH);
  // There's a LED connected to D0 which lites up when you set D0 low.  
}


void loop() {
  current_time = millis();
  double current_load = 0;
  if(state == 0  && (current_time - last_time) < state_0_timeout) {
    digitalWrite(D4, HIGH); // Continous glow when we wait for a load. 
    DPRINTLN("DEBUG :: State 0 :: time: " + String(current_time - last_time));
    Serial.println("      :: State 0 :: Waiting for load...");
    current_load = scale.get_units(5);
    DPRINTLN("DEBUG :: State 0 :: " + String(current_load));
    if(current_load - no_load > load_threshold) {
      Serial.println("      :: State 0 :: Load detected...");
      state = 1; // Going to next state. 
      digitalWrite(D4, LOW);
      delay(100);
      digitalWrite(D4, HIGH);
      delay(100);
      last_time = current_time;
    }
  } else if(state == 0 && (current_time - last_time) > state_0_timeout) {
    // We blink the orange LED a couple of times to indicate error.
    errorBlink();
    powerDown("State 0 :: Timeout :: No weight detected");
  } else if(state == 1) {
    if((current_time - last_time) % 4 < 2) {
      digitalWrite(D4, HIGH);
    } else {
      digitalWrite(D4, LOW);
    }
    current_load = scale.get_units(5);
    Serial.println("      :: State 1 :: Stabilizing...");
    DPRINTLN("DEBUG :: State 1 :: " + String(current_load));
    if((current_time - last_time) > state_1_wait) {
      DPRINTLN("DEBUG :: State 1:: Stabilization complete");
      state = 2;
      digitalWrite(D4, LOW);
      digitalWrite(D0, LOW);
      delay(100);
      digitalWrite(D0, HIGH);
      delay(100);
    }
  } else if(state == 2) {
    digitalWrite(D0, LOW); // Both blue LEDS should now be on.
    Serial.println("      :: State 2 :: Measuring...");
    // 100 samples instead of 5. Hopefully removes any noise. 
    current_load = scale.get_units(100);
    Serial.println("      :: State 2 :: Measure complete: " + String(current_load));
    state = 3; 
    // Might need to add a warning here if current_load is out of whack.

    digitalWrite(D0, HIGH);
    delay(100);
    digitalWrite(D0, LOW);
  }
  
  if(state == 3 && num_upload_fails < 10) {
      bool ret = sendToSpreadsheet(current_load);
      if(ret == false) {
        num_upload_fails++;
      } else {
        Serial.println("      :: State 3 :: Successfully uploaded to spreadsheet");
        successBlink();
        powerDown("Successful measurement");
      }
  } else if(state == 3 && num_upload_fails > 10) {
    Serial.println("ERROR :: State 3 :: Failed to upload to spreadsheet");
    // Blink LED rapidly here. 
    errorBlink();
    powerDown("Failed to upload data.");
  }

}

void calibrate() {
  double current_load = scale.get_units(5);
  Serial.println(current_load);
  if(Serial.available())
  {
    char temp = Serial.read();
    if(temp == '+' || temp == 'a') {
      calibration_factor += 10;
      Serial.println(calibration_factor);
    } else if(temp == '-' || temp == 'z') {
      calibration_factor -= 10;
      Serial.println(calibration_factor);
    }
    scale.set_scale(calibration_factor);
  }
}
void powerDown(String reason) {
  Serial.println("Going to sleep...");
  DPRINTLN("Reason: " + reason);

  scale.power_down(); // Reduces power consumption by the HX711 chip. 
  ESP.deepSleep(0); 
  
}
void successBlink() {
  for(int i = 0; i < 10; i++) {
      digitalWrite(D0, HIGH);
      delay(200);
      digitalWrite(D0, LOW);
      delay(200);  
  }
}
void errorBlink() {
  for(int i = 0; i < 10; i++) {
      digitalWrite(D4, HIGH);
      delay(200);
      digitalWrite(D4, LOW);
      delay(200);  
  }
}
bool sendToSpreadsheet(float value) {
  static int error_count = 0;
  
  client = new HTTPSRedirect(httpsPort);
  // client->setInsecure();
  client->setPrintResponseBody(true);
  client->setContentTypeHeader("application/json");
  
  if (client != nullptr){
      DPRINTLN("DEBUG :: sendToSpreadsheet :: Trying to connect...");
      client->connect(host, httpsPort);
  } else {
    Serial.println("ERROR :: sendToSpreadsheet :: Could not create client.");
    delete client;
    client = nullptr;
    return false;
  }
  
  
  DPRINTLN("DEBUG :: sendToSpreadsheet :: POST append memory data to spreadsheet:");
  payload = payload_base + "\"" + String(value, 2) + "\"}";
  DPRINTLN("DEBUG :: sendToSpreadsheet :: This is the payload: \n" + payload);
  if(client->POST(url2, host, payload)){
    delete client;
    client = nullptr;
    return true;
  } else{
    delete client;
    client = nullptr;
    return false;
  }

 
}

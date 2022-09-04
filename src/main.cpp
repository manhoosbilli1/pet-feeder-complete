#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <JC_Button_ESP.h> // https://github.com/maizy/JC_Button
#include <NewPing.h>
#include "HX711.h"

// for h-bridge..
#define motor 23

#define scaleResetPin 18
Button button(scaleResetPin); // define the button

#define TRIGGER_PIN 18                              // Arduino pin tied to trigger pin on ping sensor.
#define ECHO_PIN 19                                 // Arduino pin tied to echo pin on ping sensor.
#define MAX_DISTANCE 200                            // Maximum distance we want to ping for (in centimeters). Maximum sensor distance is rated at 400-500cm.
NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE); // NewPing setup of pins and maximum distance.
#define emptyDistance 40                            // in cm

const int LOADCELL_DOUT_PIN = 22;
const int LOADCELL_SCK_PIN = 21;
#define calibration_factor -376.5 // to be found.
HX711 scale;
// calibrate first by uploading calibrating code to it.

// Provide the token generation process info.
#include <addons/TokenHelper.h>
// Provide the RTDB payload printing info and other helper functions.
#include <addons/RTDBHelper.h>
/* 1. Define the WiFi credentials */
#define WIFI_SSID "ALI"
#define WIFI_PASSWORD "99929995"
/* 2. Define the API Key */
#define API_KEY "AIzaSyAzUdNeTEG0-Rpb6glw63cFVmYwWSmDuZI"
/* 3. Define the RTDB URL */
#define DATABASE_URL "pet-feeder-73084-default-rtdb.firebaseio.com" //<databaseName>.firebaseio.com or <databaseName>.<region>.firebasedatabase.app
/* 4. Define the user Email and password that alreadey registerd or added in your project */
#define USER_EMAIL "ayesha.zia.jan@gmail.com"
#define USER_PASSWORD "test123"

// Define Firebase Data object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
unsigned long sendDataPrevMillis = 0;

int reading;
int lastReading;

// database variables
bool needsFilling = false;
bool feedNow;
float currentWeight;
float distance;
bool detected;
int feedInterval = 3*60*1000;  //3 minutes. in terms of ms
int pour_now;
int needs_filling; 
bool justPoured; 
int pouredToday; 


//timers 
unsigned long prevFeed; 
unsigned int intervalMillis; 
int count;

void readScale()
{

  if (scale.wait_ready_timeout(200))
  {
    reading = round(scale.get_units());
    // Serial.print("Weight: ");
    // Serial.println(reading);
    if (reading != lastReading)
    {
      currentWeight = reading;
    }
    lastReading = reading;
  }
  else
  {
    Serial.println("HX711 not found.");
  }
}


void initScale()
{
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare();
}

void setup()
{
  pinMode(motor, OUTPUT);
  Serial.begin(115200);
  initScale();
  Serial.println("scale reset");
  button.begin(); // initialize the button object
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  int cnt;

  while (WiFi.status() != WL_CONNECTED)
  {
    cnt++;
    Serial.print(".");
    delay(300);
    if (cnt >= 30)
      ESP.restart();
  }


  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  /* Assign the api key (required) */
  config.api_key = API_KEY;

  /* Assign the user sign in credentials */
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

}

void loop()
{
  distance = sonar.ping_cm();   //get value to indicate wether it's time to notify the person
  if (Firebase.RTDB.getString(&fbdo, "feedInterval"))
  {
    String data = fbdo.to<String>();
    feedInterval = data.toInt();
  }
  else
  {
    Serial.println("FAILED");
    Serial.println("REASON: " + fbdo.errorReason());
  }
 
  //feed after every interval. change below line add extra 60 to make it hour.

  if(millis() - intervalMillis >= feedInterval *60 * 1000)
  {
    intervalMillis = millis(); 
    pour_now = 1;
  }

  //check if animal is detected
  if (Firebase.RTDB.getString(&fbdo, "detected"))
  {
    String data = fbdo.to<String>();
    detected = data.toInt();
    Serial.print(detected);
  }
  else
  {
    Serial.println("FAILED");
    Serial.println("REASON: " + fbdo.errorReason());
  }

  //check if bowl is empty. 
  if (Firebase.RTDB.getString(&fbdo, "needs_filling"))
  {
    String data = fbdo.to<String>();
    needs_filling = data.toInt();
    Serial.print(detected);
  }
  else
  {
    Serial.println("FAILED");
    Serial.println("REASON: " + fbdo.errorReason());
  }

  //check if tank needs refill.. change 8 to any distance in cm max = 15
  if(distance >= 8 && millis() - prevFeed >= 120000){
    prevFeed = millis(); 
    needs_filling = 1;
    if (Firebase.RTDB.setString(&fbdo, "needs_filling", "1"))
    {
      Serial.println("PASSED");
      Serial.println("PATH: " + fbdo.dataPath());
      Serial.println("TYPE: " + fbdo.dataType());
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }    
  }
  else {
    needs_filling = 0;
    if (Firebase.RTDB.setString(&fbdo, "needs_filling", "0"))
    {
      Serial.println("PASSED");
      Serial.println("PATH: " + fbdo.dataPath());
      Serial.println("TYPE: " + fbdo.dataType());
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }    
  }


  //check if trigger happened.. pour food. 
  if (detected == 1 || pour_now == 1)
  {
    justPoured = true; 
    digitalWrite(motor, HIGH);
    delay(1000);
    digitalWrite(motor, LOW);
    delay(500); 
    detected = 0;
    pour_now = 0;
    readScale(); 

    if (Firebase.RTDB.getString(&fbdo, "poured_today"))
    {
      String data = fbdo.to<String>();
      int temp = data.toInt();
      pouredToday = temp + currentWeight;  
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.setString(&fbdo, "poured_today", pouredToday))
    {
      Serial.println("PASSED");
      Serial.println("PATH: " + fbdo.dataPath());
      Serial.println("TYPE: " + fbdo.dataType());
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }


    if (Firebase.RTDB.setInt(&fbdo, "currentWeight", currentWeight))
    {
      Serial.println("PASSED");
      Serial.println("PATH: " + fbdo.dataPath());
      Serial.println("TYPE: " + fbdo.dataType());
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }

    distance = sonar.ping_cm();   //write updated food level there. 

    if (Firebase.RTDB.setInt(&fbdo, "currentFoodLevel", distance))
    {
      Serial.println("PASSED");
      Serial.println("PATH: " + fbdo.dataPath());
      Serial.println("TYPE: " + fbdo.dataType());
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.setString(&fbdo, "detected", "0"))
    {
      Serial.println("PASSED");
      Serial.println("PATH: " + fbdo.dataPath());
      Serial.println("TYPE: " + fbdo.dataType());
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.setInt(&fbdo, "pour_food", 0))
    {
      Serial.println("PASSED");
      Serial.println("PATH: " + fbdo.dataPath());
      Serial.println("TYPE: " + fbdo.dataType());
    }
    else
    {
      Serial.println("FAILED");
      Serial.println("REASON: " + fbdo.errorReason());
    }
  }

  if (Firebase.RTDB.getInt(&fbdo, "pour_food"))
  {
    String data = fbdo.to<String>();
    pour_now = data.toInt();
    Serial.print(detected);
  }
  else
  {
    Serial.println("FAILED");
    Serial.println("REASON: " + fbdo.errorReason());
  }

}

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <FastLED.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

#define FASTLED_ALLOW_INTERRUPTS 0
#define FASTLED_ESP8266_RAW_PIN_ORDER

#define DATA_PIN    D3
#define NUM_LEDS    300
CRGB leds[NUM_LEDS];

#define BRIGHTNESS          96
#define FRAMES_PER_SECOND  120

// COOLING: How much does the air cool as it rises?
// Less cooling = taller flames.  More cooling = shorter flames.
// Default 50, suggested range 20-100
#define COOLING  55

// SPARKING: What chance (out of 255) is there that a new spark will be lit?
// Higher chance = more roaring fire.  Lower chance = more flickery fire.
// Default 120, suggested range 50-200.
#define SPARKING 120

WiFiClient espClient;
PubSubClient client(espClient);


const char* ssid = "XXX";
const char* password = "XXX";
const char* mqtt_server = "3L15t4.xyz";
const char* mqtt_topic = "ddiBaum";

enum power {
  ON, OFF
};

enum animation {
  RAINBOW, JUGGLE, FIRE, CYCLON
};
uint8_t gHue = 0;
bool gReverseDirection = false;
int cur_animation = RAINBOW;
int mode_led = OFF;

int redValue = 0;
int greenValue = 177;
int blueValue = 193;

bool animation_enabled = false;
bool animation_recovery_flag = false;


static uint8_t hue = 0;
int cyclon_i = 1;
int cyclon_dir = 0;

void callback(char* topic, byte* payload, unsigned int length) {
  char jsonPayload[200];
  Serial.print("[INFO] Daten erhalten. Topic: ");
  Serial.print(topic);
  Serial.print(", Payload: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char) payload[i]);
    jsonPayload[i] = (char) payload[i];
  }
  Serial.println();
  //WICHTIG - Buffer hier anlegen und nicht global!
  StaticJsonBuffer < 200 > jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(jsonPayload);
  //Falls kein JSON vorliegt, wird die Nachricht verworfen
  if (!root.success()) {
    Serial.println("[ERROR] JSON Parsing fehlgeschlagen..");
  } else {
    Serial.println("[INFO] JSON Parsing erfolgreich..");
    //Prüfe, ob der key identifier vorhanden ist, falls nicht verwerfen
    if (root.containsKey("set_pwr")) {
      if (root["set_pwr"] == "on" and mode_led == OFF) {
        mode_led = ON;
        if (animation_recovery_flag) {
          animation_enabled = true;
        } else {
          for (int i = 0; i <= 300; i++) {
            leds[i] = CRGB( redValue, greenValue, blueValue);
          }
          FastLED.show();
        }
      }
      else if (root["set_pwr"] == "off" and mode_led == ON) {
        if (animation_enabled) {
          animation_recovery_flag = true;
        } else {
          animation_recovery_flag = false;
        }
        animation_enabled = false;
        mode_led = OFF;
        for (int i = 0; i <= 300; i++) {
          leds[i] = CRGB( 0, 0, 0);
        }
        FastLED.show();
      }
    }
    else if (root.containsKey("set_rgb") and mode_led == ON) {
      animation_enabled = false;
      if (root["set_rgb"][0].is<int>()) {
        if (root["set_rgb"][0] >= 0 && root["set_rgb"][0] <= 255) {
          redValue = root["set_rgb"][0];
        }
      }
      if (root["set_rgb"][1].is<int>()) {
        if (root["set_rgb"][1] >= 0 && root["set_rgb"][1] <= 255) {
          greenValue = root["set_rgb"][1];
        }
      }
      if (root["set_rgb"][2].is<int>()) {
        if (root["set_rgb"][2] >= 0 && root["set_rgb"][2] <= 255) {
          blueValue = root["set_rgb"][2];
        }
      }
      for (int i = 0; i <= 300; i++) {
        leds[i] = CRGB( redValue, greenValue, blueValue);
      }
      FastLED.show();
    }
    else if (root.containsKey("set_animation") and mode_led == ON) {
      animation_enabled = true;
      if (root["set_animation"] == "rainbow") {
        cur_animation = RAINBOW;
      }

      if (root["set_animation"] == "juggle") {
        cur_animation = JUGGLE;
      }
      if (root["set_animation"] == "fire") {
        cur_animation = FIRE;
      }
      if (root["set_animation"] == "cyclon") {
        cur_animation = CYCLON;
      }

    }

    else if (root.containsKey("set_melody")) {
      if (root["set_melody"] == "alleJahre") {
        digitalWrite(D5, LOW);
        delay(1000);
        digitalWrite(D5, HIGH);
      }
    }


    else if (root.containsKey("set_brightness") and mode_led == ON) {
      if (root["set_brightness"].is<int>()) {
        if (root["set_brightness"] >= 0 && root["set_brightness"] <= 100) {
          LEDS.setBrightness(root["set_brightness"]);
          if (!animation_enabled) {
            for (int i = 0; i <= 300; i++) {
              leds[i] = CRGB( redValue, greenValue, blueValue);
            }
            FastLED.show();
          }
        }
      }
    }

  }
}

void fadeall() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i].nscale8(250);
  }
}


void fire()
{
  // Array of temperature readings at each simulation cell
  static byte heat[NUM_LEDS];

  // Step 1.  Cool down every cell a little
  for ( int i = 0; i < NUM_LEDS; i++) {
    heat[i] = qsub8( heat[i],  random8(0, ((COOLING * 10) / NUM_LEDS) + 2));
  }

  // Step 2.  Heat from each cell drifts 'up' and diffuses a little
  for ( int k = NUM_LEDS - 1; k >= 2; k--) {
    heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
  }

  // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
  if ( random8() < SPARKING ) {
    int y = random8(7);
    heat[y] = qadd8( heat[y], random8(160, 255) );
  }

  // Step 4.  Map from heat cells to LED colors
  for ( int j = 0; j < NUM_LEDS; j++) {
    CRGB color = HeatColor( heat[j]);
    int pixelnumber;
    if ( gReverseDirection ) {
      pixelnumber = (NUM_LEDS - 1) - j;
    } else {
      pixelnumber = j;
    }
    leds[pixelnumber] = color;
  }
}




void rainbow()
{
  // FastLED's built-in rainbow generator
  fill_rainbow( leds, NUM_LEDS, gHue, 7);
}

void juggle() {
  // eight colored dots, weaving in and out of sync with each other
  fadeToBlackBy( leds, NUM_LEDS, 20);
  byte dothue = 0;
  for ( int i = 0; i < 8; i++) {
    leds[beatsin16( i + 7, 0, NUM_LEDS - 1 )] |= CHSV(dothue, 200, 255);
    dothue += 32;
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(mqtt_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void setup() {
  delay(3000); // 3 second delay for recovery
  pinMode(D5, INPUT);
  digitalWrite(D5, HIGH);
  FastLED.addLeds<WS2811, DATA_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 1800);
  FastLED.setBrightness(90);
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("baum");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);


}

void loop() {
  ArduinoOTA.handle();

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (animation_enabled) {
    if (cur_animation == RAINBOW) {
      rainbow();

    }
    if (cur_animation == JUGGLE) {
      juggle();
    }
    if (cur_animation == FIRE) {
      fire();
    }
    if (cur_animation == CYCLON) {
      if (cyclon_dir == 0) {
        cyclon_i++;
      }
      if (cyclon_dir == 1) {
        cyclon_i--;
      }

      if (cyclon_i == 300) {
        cyclon_dir = 1;
      }

      if (cyclon_i == 0) {
        cyclon_dir = 0;
      }

      leds[cyclon_i] = CHSV(hue++, 255, 255);

    }
    FastLED.show();
    if (cur_animation == CYCLON) {
      fadeall();
    }
    // insert a delay to keep the framerate modest
    FastLED.delay(1000 / FRAMES_PER_SECOND);



    // do some periodic updates
    EVERY_N_MILLISECONDS( 20 ) {
      gHue++;  // slowly cycle the "base color" through the rainbow
    }
  }
}

#include <PubSubClient.h>
#include <ESP8266WiFi.h>        // Include the Wi-Fi library
#include <Arduino.h>
#include <Wire.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <SimpleTimer.h>
#include <ESP8266HTTPClient.h>
#include <FS.h>
#include <AutoConnect.h>
#include <Relay.h>

typedef ESP8266WebServer WiFiWebServer;

#define PARAM_FILE      "/param.json"
#define AUX_SETTING_URI "/mqtt_setting"
#define AUX_SAVE_URI    "/mqtt_save"
#define HOME_URI    "/"

WiFiWebServer Server;
WiFiClient espClient;
PubSubClient client(espClient);

AutoConnect Portal(Server);
AutoConnectConfig Config;

long lastMsg = 0;
char msg[50];
int value = 0;

SimpleTimer timer;
int myNum = 5;          //Number of times to call the repeatMe function
int myInterval = 30000;  //time between funciton calls in millis

Relay relay = Relay(0,false);

bool stringComplete = false;  // whether the string is complete
//SoftwareSerial softSerial(3, 1); // RX, TX
uint32_t delayMS;

unsigned long   sampletime_ms1 = 3000;

String mqtt_server = "192.168.0.162";

// JSON definition of AutoConnectAux.
// Multiple AutoConnectAux can be defined in the JSON array.
// In this example, JSON is hard-coded to make it easier to understand
// the AutoConnectAux API. In practice, it will be an external content
// which separated from the sketch, as the mqtt_RSSI_FS example shows.
static const char AUX_mqtt_setting[] PROGMEM = R"raw(
[
  {
    "title": "MQTT Setting",
    "uri": "/mqtt_setting",
    "menu": true,
    "element": [
      {
        "name": "style",
        "type": "ACStyle",
        "value": "label+input,label+select{position:sticky;left:120px;width:230px!important;box-sizing:border-box;}"
      },
      {
        "name": "header",
        "type": "ACText",
        "value": "<h2>MQTT broker settings</h2>",
        "style": "text-align:center;color:#2f4f4f;padding:10px;"
      },
      {
        "name": "mqttserver",
        "type": "ACInput",
        "value": "",
        "label": "Server",
        "pattern": "^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]*[a-zA-Z0-9])\\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\\-]*[A-Za-z0-9])$",
        "placeholder": "MQTT broker server"
      },
      {
        "name": "channelId_relay",
        "type": "ACInput",
        "label": "Relay Channel ID",
        "pattern": "^(\/[a-zA-Z0-9]([a-zA-Z0-9-])*)*$"
      },
      {
        "name": "channelid_ctrl",
        "type": "ACInput",
        "label": "Telemetry Channel ID",
        "pattern": "^(\/[a-zA-Z0-9]([a-zA-Z0-9-])*)*$"
      },
      {
        "name": "newline",
        "type": "ACElement",
        "value": "<hr>"
      },
      {
        "name": "hostname",
        "type": "ACInput",
        "value": "",
        "label": "ESP host name",
        "pattern": "^([a-zA-Z0-9]([a-zA-Z0-9-])*[a-zA-Z0-9]){1,24}$"
      },
      {
        "name": "save",
        "type": "ACSubmit",
        "value": "Save&amp;Start",
        "uri": "/mqtt_save"
      },
      {
        "name": "discard",
        "type": "ACSubmit",
        "value": "Discard",
        "uri": "/_ac"
      }
    ]
  },
  {
    "title": "MQTT Setting",
    "uri": "/mqtt_save",
    "menu": false,
    "element": [
      {
        "name": "caption",
        "type": "ACText",
        "value": "<h4>Parameters saved as:</h4>",
        "style": "text-align:center;color:#2f4f4f;padding:10px;"
      },
      {
        "name": "parameters",
        "type": "ACText"
      }
    ]
  }
]
)raw";

String  channelId_relay_telemetry;
String  channelId_relay_ctrl;
String  hostName;
String s_relay;


void genTopics() {
   s_relay = channelId_relay_telemetry;
}

void getParams(AutoConnectAux& aux) {
  mqtt_server = aux["mqttserver"].value;
  mqtt_server.trim();
  channelId_relay_telemetry = aux["channelid_ctrl"].value;
  channelId_relay_telemetry.trim();
  channelId_relay_ctrl = aux["channelId_relay"].value;
  channelId_relay_ctrl.trim();
  hostName = aux["hostname"].value;
  hostName.trim();
}

// Load parameters saved with  saveParams from SPIFFS into the
// elements defined in /mqtt_setting JSON.
String loadParams(AutoConnectAux& aux, PageArgument& args) {
  (void)(args);
  File param = SPIFFS.open(PARAM_FILE, "r");
  if (param) {
    if (aux.loadElement(param)) {
      getParams(aux);
      Serial.println(PARAM_FILE " loaded");
    }
    else
      Serial.println(PARAM_FILE " failed to load");
    param.close();
  }
  else {
    Serial.println(PARAM_FILE " open failed");
#ifdef ARDUINO_ARCH_ESP32
    Serial.println("If you get error as 'SPIFFS: mount failed, -10025', Please modify with 'SPIFFS.begin(true)'.");
#endif
  }
  return String("");
}

// Save the value of each element entered by '/mqtt_setting' to the
// parameter file. The saveParams as below is a callback function of
// /mqtt_save. When invoking this handler, the input value of each
// element is already stored in '/mqtt_setting'.
// In Sketch, you can output to stream its elements specified by name.
String saveParams(AutoConnectAux& aux, PageArgument& args) {
  // The 'where()' function returns the AutoConnectAux that caused
  // the transition to this page.
  AutoConnectAux&   mqtt_setting = *Portal.aux(Portal.where());
  getParams(mqtt_setting);
  AutoConnectInput& mqttserver = mqtt_setting["mqttserver"].as<AutoConnectInput>();

  // The entered value is owned by AutoConnectAux of /mqtt_setting.
  // To retrieve the elements of /mqtt_setting, it is necessary to get
  // the AutoConnectAux object of /mqtt_setting.
  File param = SPIFFS.open(PARAM_FILE, "w");
  mqtt_setting.saveElement(param, { "mqttserver", "channelId_relay", "channelid_ctrl", "hostname" });
  param.close();

  // Echo back saved parameters to AutoConnectAux page.
  AutoConnectText&  echo = aux["web"].as<AutoConnectText>();
  echo.value = "Server: " + mqtt_server;
  echo.value += mqttserver.isValid() ? String(" (OK)") : String(" (ERR)");
  echo.value += "<br>Channel Relay ID: " + channelId_relay_ctrl + "<br>";
  echo.value += "<br>Channel Telemetry ID: " + channelId_relay_telemetry + "<br>";
  echo.value += "ESP host name: " + hostName + "<br>";

  return String("");
}

void callback(char* topic, byte* payload, unsigned int length) {
  if ((char)payload[0] == '1') {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (unsigned int i = 0; i < length; i++) {
      Serial.println((char)payload[i]);
    }
    relay.turnOn();

    if (relay.getState())
      client.publish(String(channelId_relay_telemetry).c_str(), "ON");
    else
      client.publish(String(channelId_relay_telemetry).c_str(), "OFF");
  }
  else if ((char)payload[0] == '0') {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (unsigned int i = 0; i < length; i++) {
      Serial.println((char)payload[i]);
    }
    relay.turnOff();

    if (relay.getState())
      client.publish(String(channelId_relay_telemetry).c_str(), "ON");
    else
      client.publish(String(channelId_relay_telemetry).c_str(), "OFF");
  }
  else if ((char)payload[0] == 'R') {
    Serial.println("Resetting ESP");
    ESP.restart(); //ESP.reset();
  }
}

void reconnect() {
  int retries = 0;

  // Loop until we're reconnected
  while (!client.connected()) {
    retries++;
    Serial.print("MQTT connection...");
    // Attempt to connect
    char buffer[10]="";
    sprintf(buffer, "espRelay_%i", ESP.getChipId());
    if (client.connect(buffer)) {
      Serial.println("connected");
      // ... and resubscribe
      client.subscribe(String(channelId_relay_ctrl).c_str());
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
      if (retries>2) {
        retries = 0;
        Serial.println("Resetting ESP");
        ESP.restart(); //ESP.reset();     // Send the IP address of the ESP8266 to the computer
      }
    }
  }
}

    void getState() {
      if (relay.getState())
        client.publish(String(channelId_relay_telemetry).c_str(), "ON");
      else
        client.publish(String(channelId_relay_telemetry).c_str(), "OFF");
      }

    void setup() {
      Serial.begin(9600);       // Start the Serial communication to send messages to the computer

      delay(10);
      Serial.println('\n');
      SPIFFS.begin();

      if (Portal.load(FPSTR(AUX_mqtt_setting))) {
        AutoConnectAux& mqtt_setting = *Portal.aux(AUX_SETTING_URI);
        PageArgument  args;
        loadParams(mqtt_setting, args);
        if (hostName.length()) {
          Config.hostName = hostName;
          Serial.println("hostname set to " + Config.hostName);
        }
        Config.bootUri = AC_ONBOOTURI_HOME;
        Config.title = "Relay";
        Portal.config(Config);

        Portal.on(AUX_SETTING_URI, loadParams);
        Portal.on(AUX_SAVE_URI, saveParams);
      }
      else
        Serial.println("load error");
      //root.insert(Server);
      if (Portal.begin()) {
        /*ESP8266WebServer& IntServer = Portal.host();
        IntServer.on("/", handleRoot);
        Portal.onNotFound(handleNotFound);    // For only onNotFound.*/
        Serial.println("Connection established!");
        Serial.print("IP address:\t");
        Serial.println(WiFi.localIP());         // Send the IP address of the ESP8266 to the computer
      }

      genTopics();
      // Port defaults to 8266
      //ArduinoOTA.setPort(8266);
      // Hostname defaults to esp8266-[ChipID]

      ArduinoOTA.setHostname(String(Config.hostName).c_str());

      // No authentication by default
      ArduinoOTA.setPassword("AirQadmin");

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

      Serial.println("Initialize ArduinoOTA");
      ArduinoOTA.begin();

      client.setServer(mqtt_server.c_str(), 1883);
      client.setCallback(callback);
      Serial.println("Ready!");

      //delayMS = sensor.min_delay / 1000;
      relay.begin();
      Serial.print("delayMS = ");Serial.println(delayMS);
      timer.setInterval(myInterval, getState);
    }


void loop() {
  Portal.handleClient();
  if (!client.connected()) {
  reconnect();
  }

  ArduinoOTA.handle();
  timer.run();
  client.loop();
}

// =================================================================================================
// eModbus: Copyright 2020 by Michael Harwerth, Bert Melis and the contributors to ModbusClient
//               MIT license - see license.md for details
// =================================================================================================

// Example code to show the usage of the eModbus library. 
// Please refer to root/Readme.md for a full description.

// Includes: <Arduino.h> for Serial etc., WiFi.h for WiFi support
#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <ModbusClientTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <iostream>
#include <unordered_map>

using namespace std;

// Include the header for the ModbusClient TCP style
#include "ModbusClientTCP.h"

// Define the GPIO pin where the LED is connected
#define LED1_GREEN_PIN 4
#define LED2_RED_PIN   2
// Define the GPIO pin where the relay is connected
#define RELAY1_PIN 14

#define CHRGCTL_WIFI_ENABLED_DEFAULT  false
#define CHRGCTL_ETHERNET_ENABLED_DEFAULT  true

#define CHRGCTL_WIFI_IP_ADDRESS  "10.4.1.94"
#define CHRGCTL_WIFI_IP_MASK     "255.255.255.0"
#define CHRGCTL_WIFI_IP_GATEWAY  "0.0.0.0"

typedef enum {
  LED_GREEN = LED1_GREEN_PIN,
  LED_RED = LED2_RED_PIN
} LedColor_e;

// Ethernet settings (optional if you want to use Ethernet)
const char *hostname = "CHRG_CTRL";

#define CHRGCTL_DEFAULT_IP_ADDRESS  "10.4.0.94"
#define CHRGCTRL_DEFAULT_IP_MASK    "255.255.255.0"
#define CHRGCTRL_DEFAULT_IP_GATEWAY "0.0.0.0"

#define CERBOGX_DEFAULT_IP_ADDRESS "10.4.0.5"
#define CERBOGX_DEFAULT_UNITID     "100"

#define HTTP_USER "admin"
#define HTTP_PASS "admin"

#define CHRGCTL_WIFI_SSID ""
#define CHRGCTL_WIFI_PASS ""

// Mapping from RegisterNum (uint16_t) to key (unsigned long)
unordered_map<uint16_t, unsigned long> registerMap;

String HttpUser;
String HttpPass;

uint8_t   ServerUnitId;

WiFiClient theClient;                          // Set up a client
AsyncWebServer server(80);                     // Set up a web server
ModbusClientTCP MB(theClient);                 // Set up a Modbus TCP client
Preferences preferences;                       // Set up a preferences object


uint32_t LastModbusError = 0;                  // Last Modbus error code

//Define which registers to read
//VE.BUS Devices from Multiplus (comm.victronenergy.vebus)
#define REG_MULTIPLUS_DC_OUT  26   //comm.victronenergy.vebus -  Battery voltage
#define REG_MULTIPLUS_CURRENT 27   //comm.victronenergy.vebus -  Battery current
#define REG_MULTIPLUS_STATE   31   //comm.victronenergy.vebus -  State of Multiplus 0=Off;1=Low Power;2=Fault;3=Bulk;4=Absorption;5=Float;6=Storage;7=Equalize;8=Passthru;9=Inverting;10=Power assist;11=Power supply;244=Sustain;252=External control
//com.victronenergy.system (synthetic information)
#define REG_STATE_OF_CHARGE   843  //com.victronenergy.system -  State of charge (system)
#define REG_BATTERY_CURRENT   841  //com.victronenergy.system -  Battery current

#define NUMBER_OF_REGISTERS   5

typedef enum {
  CHARGING,
  INVERTING,
  DISCHARGING,
  OFF
}
MultiplusStatus_e;

typedef enum {
  ABSORPTION,
  FLOAT,
  BULK,
  DISCHARGE,
  OTHER
}
BatteryMode_e;

typedef enum {
  MSTAT_OK,
  MSTAT_ERROR
}
ModbusStatus_e;

uint32_t RelayOnTimeRef = 0;
bool     RelayIsOn = false;

float SystemBatterySOC     = 0.0;
float SystemBatteryCurrent = 0.0;
float MultiplusVDCOut = 0.0;
float MultiplusCurrent = 0.0;
MultiplusStatus_e MultiplusStatus = CHARGING;
BatteryMode_e ChargingMode = OTHER;
ModbusStatus_e ModbusStatus = MSTAT_ERROR;
uint32_t ModbusMessageCounter = 0;
uint32_t ModbusRequestCounter = 0;

String MultiplusStatusStr[] = {"Charging", "Inverting", "Discharging", "Off"};
String BatteryModeStr[] = {"Absorption", "Float", "Bulk", "Discharge", "Other"};
String ModbusStatusStr[] = {"OK", "ERROR"};

// Function prototypes
void handleData(ModbusMessage response, uint32_t token);
void handleError(Error error, uint32_t token) ;

bool wifiEnabled;
bool ethernetEnabled;

String MultiplusStatusAsString(MultiplusStatus_e status) {
  return MultiplusStatusStr[status];
}

String BatteryModeAsString(BatteryMode_e mode) {
  return BatteryModeStr[mode];
}


// Function to set the relay output
void setRelayOutput(bool state) {
  Serial.println("Relay state: " + String(state));
  digitalWrite(RELAY1_PIN, state?LOW:HIGH);
}

// Function to set the LED output
void setLed(LedColor_e Led, bool state) {
  switch (Led) {
    case LED_GREEN:
      digitalWrite(LED1_GREEN_PIN, state ? HIGH : LOW);
      break;
    case LED_RED:
      digitalWrite(LED2_RED_PIN, state ? HIGH : LOW);
      break;
    default:
      break;
  }
}

// Event handler for Ethernet events
void ethEvent(WiFiEvent_t event) {
  
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      // Set hostname
      ETH.setHostname(hostname);
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("ETH IP Address: ");
      Serial.println(ETH.localIP());
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      break;
    case 	ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      break;
    default:
      Serial.print("ETH Event ???? = ");
      Serial.println(event);

      break;
  }
}

void getNetworkConfig(IPAddress &local_IP, IPAddress &gateway, IPAddress &subnet, bool isWiFi) {
  if (isWiFi) {
    Serial.println("WiFi IP Address: " + preferences.getString("wifi_ip", CHRGCTL_WIFI_IP_ADDRESS));
    Serial.println("WiFi Subnet Mask: " + preferences.getString("wifi_mask", CHRGCTL_WIFI_IP_MASK)); 
    Serial.println("WiFi Gateway: " + preferences.getString("wifi_gateway", CHRGCTL_WIFI_IP_GATEWAY));

    if (!local_IP.fromString(preferences.getString("wifi_ip", CHRGCTL_WIFI_IP_ADDRESS))) {
      Serial.println("Invalid default WiFi IP address");
      preferences.putString("wifi_ip", CHRGCTL_WIFI_IP_ADDRESS);
      ESP.restart();
    }

    if (!subnet.fromString(preferences.getString("wifi_mask", CHRGCTL_WIFI_IP_MASK))) {
      Serial.println("Invalid default WiFi subnet mask");
      preferences.putString("wifi_mask", CHRGCTL_WIFI_IP_MASK);
      ESP.restart();
    }

    if (!gateway.fromString(preferences.getString("wifi_gateway", CHRGCTL_WIFI_IP_GATEWAY))) {
      Serial.println("Invalid default WiFi gateway address");
      preferences.putString("wifi_gateway", CHRGCTL_WIFI_IP_GATEWAY);
      ESP.restart();
    }
  } else {
    Serial.println("IP Address: " + preferences.getString("chrg_ip", CHRGCTL_DEFAULT_IP_ADDRESS));
    Serial.println("Subnet Mask: " + preferences.getString("chrg_mask", CHRGCTRL_DEFAULT_IP_MASK)); 
    Serial.println("Gateway: " + preferences.getString("chrg_gateway", CHRGCTRL_DEFAULT_IP_GATEWAY));

    if (!local_IP.fromString(preferences.getString("chrg_ip", CHRGCTL_DEFAULT_IP_ADDRESS))) {
      Serial.println("Invalid default IP address");
      preferences.putString("chrg_ip", CHRGCTL_DEFAULT_IP_ADDRESS);
      ESP.restart();
    }

    if (!subnet.fromString(preferences.getString("chrg_mask", CHRGCTRL_DEFAULT_IP_MASK))) {
      Serial.println("Invalid default subnet mask");
      preferences.putString("chrg_mask", CHRGCTRL_DEFAULT_IP_MASK);
      ESP.restart();
    }

    if (!gateway.fromString(preferences.getString("chrg_gateway", CHRGCTRL_DEFAULT_IP_GATEWAY))) {
      Serial.println("Invalid default gateway address");
      preferences.putString("chrg_gateway", CHRGCTRL_DEFAULT_IP_GATEWAY);
      ESP.restart();
    }
  }
}

void initEthernet()
{
IPAddress local_IP, gateway, subnet;

  getNetworkConfig(local_IP, gateway, subnet, false/*isWifi*/);

  // Initialize Ethernet
  ETH.begin();
  ETH.setHostname(hostname);
  
  // Configure Ethernet with fixed IP
  if (!ETH.config(local_IP, gateway, subnet)) {
    Serial.println("ETH Config Failed");
  } else {
    Serial.print("ETH IP Address: ");
    Serial.println(ETH.localIP());
  }
}

void initWiFi() {
  // Connect to Wi-Fi
  String ssid = preferences.getString("chrg_wifi_ssid", "");
  String password = preferences.getString("chrg_wifi_pass", "");
  IPAddress local_IP, gateway, subnet;

  getNetworkConfig(local_IP, gateway, subnet, true/*isWifi*/);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println("Connected to WiFi");

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
  }

  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}


// Function to check if the HTTP client is authenticated
bool HttpIsAuthenticated(AsyncWebServerRequest *request) {
  if (request->authenticate(HttpUser.c_str(), HttpPass.c_str())) {
    return true;
  } else {
    request->requestAuthentication("ChargerCtl", true); // Request digest authentication for realm "ChargerCtl"
    return false;
  }
}

// Function to initialize the web server
void InitWebServer(void) {
  HttpUser = preferences.getString("http_user", HTTP_USER);
  HttpPass = HTTP_PASS;
  Serial.println("User " + HttpUser + " Pass " + HttpPass);
  
  // Serve static files with authentication
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setAuthentication(HttpUser.c_str(), HttpPass.c_str());

  // JSON endpoint for system status
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!HttpIsAuthenticated(request)) return;

    JsonDocument doc;
    doc["multiplus_status"] = MultiplusStatusAsString(MultiplusStatus);
    doc["multiplus_dc_out"] = MultiplusVDCOut;
    doc["multiplus_current"] = MultiplusCurrent;
    doc["charging_mode"] = BatteryModeAsString(ChargingMode);
    doc["modbus_status"] = ModbusStatusStr[ModbusStatus];
    doc["modbus_message_counter"] = ModbusMessageCounter;

    String json;
    serializeJson(doc, json);

    request->send(200, "application/json", json);
  });
  
  // JSON endpoint for retrieving config
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!HttpIsAuthenticated(request)) return;
 
    JsonDocument doc;
    doc["chrg_ip"] = preferences.getString("chrg_ip", CHRGCTL_DEFAULT_IP_ADDRESS);
    doc["chrg_mask"] = preferences.getString("chrg_mask", CHRGCTRL_DEFAULT_IP_MASK);
    doc["chrg_gateway"] = preferences.getString("chrg_gateway", CHRGCTRL_DEFAULT_IP_GATEWAY);
    doc["cerbo_unitid"] = preferences.getString("cerbo_unitid", CERBOGX_DEFAULT_UNITID);
    doc["cerbo_ip"] = preferences.getString("cerbo_ip", CERBOGX_DEFAULT_IP_ADDRESS);
    doc["chrg_password"] = preferences.getString("http_pass", HTTP_PASS);
    doc["chrg_wifi_ssid"] = preferences.getString("chrg_wifi_ssid", CHRGCTL_WIFI_SSID);
    doc["chrg_wifi_pass"] = preferences.getString("chrg_wifi_pass", CHRGCTL_WIFI_PASS);
    doc["wifi_enabled"] = preferences.getBool("wifi_enabled", CHRGCTL_WIFI_ENABLED_DEFAULT);
    doc["ethernet_enabled"] = preferences.getBool("eth_enabled", CHRGCTL_ETHERNET_ENABLED_DEFAULT);
    doc["wifi_ip"] = preferences.getString("wifi_ip", CHRGCTL_WIFI_IP_ADDRESS);
    doc["wifi_mask"] = preferences.getString("wifi_mask", CHRGCTL_WIFI_IP_MASK);
    doc["wifi_gateway"] = preferences.getString("wifi_gateway", CHRGCTL_WIFI_IP_GATEWAY);

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/save_config", HTTP_POST, [](AsyncWebServerRequest *request){},
    NULL,  // This is required when using body handlers
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (!HttpIsAuthenticated(request)) return;
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);
        if (error) {
            request->send(400, "application/json", "{\"message\":\"Invalid JSON\"}");
            return;
        }

        preferences.putString("chrg_ip", doc["chrg_ip"].as<String>());
        preferences.putString("chrg_mask", doc["chrg_mask"].as<String>());
        preferences.putString("chrg_gateway", doc["chrg_gateway"].as<String>());
        preferences.putString("cerbo_ip", doc["cerbo_ip"].as<String>());
        preferences.putString("cerbo_unitid", doc["cerbo_unitid"].as<String>());
        if (doc["chrg_password"].as<String>().length() != 0) 
          preferences.putString("http_pass", doc["chrg_password"].as<String>());

        preferences.putString("http_pass", doc["chrg_password"].as<String>());
        preferences.putString("chrg_wifi_ssid", doc["chrg_wifi_ssid"].as<String>());
        preferences.putString("chrg_wifi_pass", doc["chrg_wifi_pass"].as<String>());
        preferences.putBool("wifi_enabled", doc["wifi_enabled"].as<bool>());
        preferences.putBool("eth_enabled", doc["ethernet_enabled"].as<bool>());
        preferences.putString("wifi_ip", doc["wifi_ip"].as<String>());
        preferences.putString("wifi_mask", doc["wifi_mask"].as<String>());
        preferences.putString("wifi_gateway", doc["wifi_gateway"].as<String>());

        Serial.println("wifi_enabled: " + preferences.getBool("wifi_enabled", CHRGCTL_WIFI_ENABLED_DEFAULT));
        Serial.println("ethernet_enabled: " + preferences.getBool("eth_enabled", CHRGCTL_ETHERNET_ENABLED_DEFAULT));
        Serial.println("chrg_password: " + preferences.getString("http_pass", HTTP_PASS));
        request->send(200, "application/json", "{\"message\":\"Configuration Saved!\"}");
    }
  );
  server.begin();

}

void InitModbusClient() {
  
  IPAddress CerboGXIp;

  if (!CerboGXIp.fromString(preferences.getString("cerbo_ip", CERBOGX_DEFAULT_IP_ADDRESS))) {
    Serial.println("Invalid default IP address");
    preferences.putString("cerbo_ip", CERBOGX_DEFAULT_IP_ADDRESS);
    ESP.restart();
  }

  ServerUnitId = preferences.getString("cerbo_unitid", CERBOGX_DEFAULT_UNITID).toInt();
  if (ServerUnitId == 0) {
    Serial.println("Invalid default Unit ID");
    preferences.putString("cerbo_unitid", CERBOGX_DEFAULT_UNITID);
    ESP.restart();
  }

  Serial.println("CerboGX IP: " + CerboGXIp.toString() + " Unit ID: " + String(ServerUnitId));

  // Set up ModbusTCP client.
  MB.onDataHandler(&handleData);   // - provide onData handler function
  
  MB.onErrorHandler(&handleError); // - provide onError handler function
  // Set message timeout to 2000ms and interval between requests to the same host to 200ms
  MB.setTimeout(2000, 200);
  // Start ModbusTCP background task
  MB.begin();

  // Set Modbus TCP server address and port number
  MB.setTarget(CerboGXIp, 502);

}

// Setup() - initialization happens here
void setup() {
// Init Serial monitor
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println("__ OK __");

  // Initialize the LED pin
  pinMode(LED1_GREEN_PIN, OUTPUT);
  pinMode(LED2_RED_PIN, OUTPUT);

  // Initialize the relay pin
  pinMode(RELAY1_PIN, OUTPUT);
  setRelayOutput(false);
  
  // Register the event handler
  WiFi.onEvent(ethEvent);
  
  preferences.begin("config", false);

  wifiEnabled = preferences.getBool("wifi_enabled", CHRGCTL_WIFI_ENABLED_DEFAULT);
  ethernetEnabled = preferences.getBool("eth_enabled", CHRGCTL_ETHERNET_ENABLED_DEFAULT);

  if (wifiEnabled)
    initWiFi();

  if (ethernetEnabled) 
    initEthernet();

  //Init LittleFS and start the web server
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
    return;
  }


  // Set up the web server
  InitWebServer();

  InitModbusClient();

}

// Define an onError handler function to receive error responses
// Arguments are the error code returned and a user-supplied token to identify the causing request
void handleError(Error error, uint32_t token) 
{
  ModbusStatus = MSTAT_ERROR;
  if (error == LastModbusError) return;
  // ModbusError wraps the error code and provides a readable error message for it
  ModbusError me(error);
  Serial.printf("Error response: %02X - %s\n", (int)me, (const char *)me);
  LastModbusError = error;
}

// Function to associate a RegisterNum with a key
void setRegisterKey(uint16_t registerNum, uint32_t key) {
  registerMap[registerNum] = key;
}

// Function to find the RegisterNum for a given key
bool searchByKey(unsigned long key, uint16_t &registerNum) {
  for (const auto &pair : registerMap) {
      if (pair.second == key) {
          registerNum = pair.first;
          return true; // Key found
      }
  }
  return false; // Key not found
}

// Define an onData handler function to receive the regular responses
// Arguments are the message plus a user-supplied token to identify the causing request
void handleData(ModbusMessage response, uint32_t token) 
{
  ModbusStatus = MSTAT_OK;
  LastModbusError = SUCCESS;
  ModbusMessageCounter++;

  uint16_t RegisterAddress;
  uint16_t NbWords;
  uint8_t  FunctionCode;
  uint8_t  UnitID;
  bool DataChanged = false;

  FunctionCode = response.getFunctionCode();
  UnitID = response.getServerID();
  if (FunctionCode == READ_HOLD_REGISTER) {

    if (!searchByKey(token, RegisterAddress)) {
      Serial.println("Key not found");
      return;
    }
    NbWords = (response.size() - 3) / 2;
    Serial.printf("Response: UnitId=%d, FC=%d, Token=%08X, length=%d: Register Address %d, Number of Words %d\n", UnitID, FunctionCode, token, response.size(), RegisterAddress, NbWords);  
    for (int i = 0; i < response.size(); i++) {
      uint8_t value;
      response.get(i, value);
      Serial.printf("%02X ", value);
    }
    Serial.println();

    if (RegisterAddress == REG_STATE_OF_CHARGE) {
        uint16_t URegisterValue;
        response.get(3, URegisterValue);
        if ((float)URegisterValue != SystemBatterySOC) {
          SystemBatterySOC = URegisterValue;
          DataChanged = true;
        }
    } else if (RegisterAddress == REG_BATTERY_CURRENT) {
        int16_t  IRegisterValue;
        response.get(3, IRegisterValue);
        float NewVal = ((float)IRegisterValue/(float)10.0);
        if (NewVal != SystemBatteryCurrent) {
          SystemBatteryCurrent = NewVal;
          DataChanged = true;
        }
    } else if (RegisterAddress == REG_MULTIPLUS_DC_OUT) {
        uint16_t URegisterValue;
        response.get(3, URegisterValue);
        float NewVal = ((float)URegisterValue/(float)100.0);
        if (NewVal != MultiplusVDCOut) {
          MultiplusVDCOut = NewVal;
          DataChanged = true;
        }
    } else if (RegisterAddress == REG_MULTIPLUS_CURRENT) {
        int16_t IRegisterValue;
        response.get(3, IRegisterValue);
        float NewVal = ((float)IRegisterValue/(float)10.0);
        if (NewVal != MultiplusCurrent) {
          MultiplusCurrent = NewVal;
          DataChanged = true;
        }
    } else if (RegisterAddress == REG_MULTIPLUS_STATE) {
        uint16_t URegisterValue;
        response.get(3, URegisterValue);  // 0=Off;1=Low Power;2=Fault;3=Bulk;4=Absorption;5=Float;6=Storage;7=Equalize;8=Passthru;9=Inverting;10=Power assist;11=Power supply;244=Sustain;252=External control

        switch (URegisterValue)
        {
          case 0:
            MultiplusStatus = OFF;
            break;
          case 3:
          case 4:
          case 5:
            MultiplusStatus = CHARGING;
            switch (URegisterValue)
            {
              case 3:
                ChargingMode = BULK;
                break;
              case 4:
                ChargingMode = ABSORPTION;
                break;
              case 5:
                ChargingMode = FLOAT;
                break;
            }
            break;
          case 9:
            MultiplusStatus = INVERTING;
            break;
          default:
            MultiplusStatus = DISCHARGING;
            break;
        }
        if (MultiplusStatus != CHARGING) {
          if (SystemBatteryCurrent < 0.0) {
            ChargingMode = DISCHARGE;
          }
          else
          {
            ChargingMode = OTHER;
          }
        }
        DataChanged = true;
    }

    if (DataChanged) {
      Serial.printf("Multiplus Status: %d, Charging Mode: %d", MultiplusStatus, ChargingMode);
      if ((MultiplusStatus == CHARGING) && (ChargingMode == BULK)) {
        setRelayOutput(true);
        RelayIsOn = true;
        RelayOnTimeRef = millis();
      } else {
        RelayIsOn = false;
        setRelayOutput(false);
      }
    }
  }
}

bool readHoldingRegister(uint8_t unitId, uint16_t registerNum) {
  uint32_t token = ModbusRequestCounter++;
  bool ret = false;

  if (MB.pendingRequests() > NUMBER_OF_REGISTERS) 
    return false;  

  Error err = MB.addRequest(token, ServerUnitId, READ_HOLD_REGISTER, registerNum, 1);
  if (err != SUCCESS) {
    ModbusError e(err);
    Serial.printf("Error creating request: %02X - %s\n", (int)e, (const char *)e);
  }
  else
  {
    setRegisterKey(registerNum, token);
    ret = true;
  }
  return ret;
}

uint32_t diffTime(uint32_t previousTime) {
  uint32_t currentTime = millis();

  if (currentTime < previousTime) {
    return (UINT32_MAX - previousTime) + currentTime;
  } else {
    return currentTime - previousTime;
  } 
  
}

// loop() - nothing done here today!
void loop() {
  
  // Check if the relay is on and we did not receive a new status for 3 seconds
  if (RelayIsOn) {
    if (diffTime(RelayOnTimeRef) > 10000) {
      setRelayOutput(false);
      RelayIsOn = false;
    }
  }
  readHoldingRegister(100, REG_STATE_OF_CHARGE);
  readHoldingRegister(100, REG_BATTERY_CURRENT);
  readHoldingRegister(ServerUnitId, REG_MULTIPLUS_DC_OUT);
  readHoldingRegister(ServerUnitId, REG_MULTIPLUS_CURRENT);
  readHoldingRegister(ServerUnitId, REG_MULTIPLUS_STATE);
  delay(3000);
}

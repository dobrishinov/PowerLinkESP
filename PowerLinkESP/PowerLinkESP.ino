#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SoftwareSerial.h>
#include <TaskScheduler.h>
#include <ArduinoOTA.h>

// EEPROM addresses for SSID, password, IP, subnet mask, and gateway
#define SSID_ADDR 0
#define PASS_ADDR 32
#define IP_ADDR 64
#define SUBNET_ADDR 96
#define GATEWAY_ADDR 128

// Fallback SSID and password
String uniqueDeviceName = "LabPowerSupply_" + String(ESP.getChipId());
const char* fallbackSSID = uniqueDeviceName.c_str();
const char* fallbackPassword = "12345678"; //Change this default password if you want to have different password for the AP

const int RXPin = D6; // Wemos D1 Mini RX pin
const int TXPin = D5; // Wemos D1 Mini TX pin

SoftwareSerial virtualSerial(RXPin, TXPin);
Scheduler runner;
AsyncWebServer server(80);

String voltageCommand = "<12000000000>"; // Default voltage command
String currentCommand = "<14000000000>"; // Default current command

float voltageValue = 0.0;
float currentValue = 0.0;
float powerValue = 0.0;

void sendVoltageCommand() {
  String response = sendCommandAndWait(voltageCommand);
  if (!response.isEmpty()) {
    handleVoltageCommand(response);
  }
}

void sendCurrentCommand() {
  String response = sendCommandAndWait(currentCommand);
  if (!response.isEmpty()) {
    handleCurrentCommand(response);
  }
}

String sendCommandAndWait(String command) {
  virtualSerial.print(command);
  // Wait for response
  unsigned long startTime = millis();
  while (!virtualSerial.available()) {
    if (millis() - startTime > 2000) { // Timeout after 2 seconds
      Serial.println("Timeout occurred while waiting for response from Power supply.");
      return "";
    }
  }
  // Read response
  String response = virtualSerial.readStringUntil('>');
  return response;
}

void handleVoltageCommand(String response) {
  // Extract data bits from the response
  int voltage = response.substring(4, 9).toInt();
  // Calculate voltage value (assuming data bits are in the format provided)
  voltageValue = voltage / 1000.0;
  // Calculate power
  powerValue = voltageValue * currentValue;
}

void handleCurrentCommand(String response) {
  // Extract data bits from the response
  int current = response.substring(4, 9).toInt();
  // Calculate current value (assuming data bits are in the format provided)
  currentValue = current / 1000.0;
  // Calculate power
  powerValue = voltageValue * currentValue;
}

void handleData(AsyncWebServerRequest *request) {
  String data = "{\"voltage\": " + String(voltageValue, 3) + ", \"current\": " + String(currentValue, 3) + ", \"power\": " + String(powerValue, 3) + "}";
  request->send(200, "application/json", data);
}

Task taskSendVoltage(500, TASK_FOREVER, &sendVoltageCommand);
Task taskSendCurrent(600, TASK_FOREVER, &sendCurrentCommand);

void setup() {
  // Initialize Serial Monitor
  Serial.begin(9600);
  // Initialize SoftwareSerial
  virtualSerial.begin(9600);
  // Initialize EEPROM
  EEPROM.begin(512);

  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
    return;
  }

  // Connect to WiFi using stored credentials or fallback
  connectWiFi();
  
  // Initialize tasks
  runner.init();
  runner.addTask(taskSendVoltage);
  runner.addTask(taskSendCurrent);
  
  // Enable tasks
  taskSendVoltage.enable();
  taskSendCurrent.enable();

  // Check if stored IP, subnet mask, or gateway in EEPROM are missing
  String storedIP = readStringFromEEPROM(IP_ADDR);
  String storedSubnetMask = readStringFromEEPROM(SUBNET_ADDR);
  String storedGateway = readStringFromEEPROM(GATEWAY_ADDR);

  if (storedIP.isEmpty() || storedSubnetMask.isEmpty() || storedGateway.isEmpty()) {
    Serial.println("Stored network configuration missing or incomplete. Using dynamic IP.");
    WiFi.config(0U, 0U, 0U); // Clear existing static IP configuration
  } else {
    // Configure static IP from EEPROM
    IPAddress ip, mask, gw;
    ip.fromString(storedIP);
    mask.fromString(storedSubnetMask);
    gw.fromString(storedGateway);
    WiFi.config(ip, gw, mask);
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleData(request);
  });
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleConfigPage(request);
  });
  server.on("/setconfig", HTTP_POST, [](AsyncWebServerRequest *request) {
    handleSetConfig(request);
  });
  server.on("/chart.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/chart.js")) {
      request->send(LittleFS, "/chart.js", "application/javascript");
    } else {
      request->send(404, "text/plain", "File Not Found");
    }
  });
  server.begin();
  Serial.println("HTTP server started");
  
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");
  
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
    // Print STA IP address
    Serial.println("IP Address: " + WiFi.localIP().toString());
  } else {
    // Print AP IP address
    Serial.println("IP Address: " + WiFi.softAPIP().toString());
  }
}

void loop() {
  runner.execute();
  ArduinoOTA.handle();
}

void connectWiFi() {
  String ssid = readStringFromEEPROM(SSID_ADDR);
  String password = readStringFromEEPROM(PASS_ADDR);

  // Attempt to connect to stored WiFi network
  if (ssid.length() > 0 && password.length() > 0) {
    Serial.println("Connecting to stored WiFi...");
    WiFi.hostname(uniqueDeviceName.c_str());
    WiFi.begin(ssid.c_str(), password.c_str());
  } else {
    Serial.println("Connecting to fallback WiFi...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(fallbackSSID, fallbackPassword);
  }

  // Wait for connection to be established
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  // If connection failed, start the access point
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nConnection failed. Starting AP...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(fallbackSSID, fallbackPassword);
  } else {
    Serial.println("\nConnected to WiFi!");
  }
}

void handleConfigPage(AsyncWebServerRequest *request) {
  String connectedSSID = "";
  String connectedPass = "";
  String ipAddress = "";

  // Check if device is in STA mode and connected to WiFi
  if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
    connectedSSID = WiFi.SSID();
    connectedPass = WiFi.psk();
    ipAddress = WiFi.localIP().toString();
  } else {
    ipAddress = WiFi.softAPIP().toString();
  }
  // Load HTML file from LittleFS
  File htmlFile = LittleFS.open("/config_page.html", "r");
  if (htmlFile) {
    String page = htmlFile.readString();
    htmlFile.close();

    // Replace placeholders in HTML template with actual values
    page.replace("$CONNECTED_SSID$", connectedSSID);
    page.replace("$CONNECTED_PASS$", connectedPass);
    page.replace("$IP_ADDRESS$", ipAddress);

    request->send(200, "text/html", page);
  } else {
    request->send(500, "text/plain", "Failed to load configuration page.");
  }
}

void handleSetConfig(AsyncWebServerRequest *request) {
  String ssid = request->arg("ssid");
  String password = request->arg("password");
  String staticIP = request->arg("static_ip");
  String subnetMask = request->arg("subnet_mask");
  String gateway = request->arg("gateway");
  // Store SSID and password in EEPROM
  writeStringToEEPROM(SSID_ADDR, ssid);
  writeStringToEEPROM(PASS_ADDR, password);
  EEPROM.commit();

  // Check if static IP settings are provided
  if (!staticIP.isEmpty() && !subnetMask.isEmpty() && !gateway.isEmpty()) {
    // Store static IP configuration in EEPROM
    writeStringToEEPROM(IP_ADDR, staticIP);
    writeStringToEEPROM(SUBNET_ADDR, subnetMask);
    writeStringToEEPROM(GATEWAY_ADDR, gateway);
    EEPROM.commit();
    ESP.reset();
    // Set static IP configuration
//    IPAddress ip, mask, gw;
//    ip.fromString(staticIP);
//    mask.fromString(subnetMask);
//    gw.fromString(gateway);
//    WiFi.config(ip, gw, mask);
  } else {
    // Erase static IP configuration from EEPROM
    for (int i = IP_ADDR; i < GATEWAY_ADDR + 32; ++i) {
      EEPROM.write(i, 0);
    }
    EEPROM.commit();

    // Clear existing static IP configuration
    WiFi.config(0U, 0U, 0U);
    ESP.reset();
  }

  // Attempt to connect to the new WiFi network
  connectWiFi();

  // Send response to client
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    request->send(200, "text/plain", "Configuration saved and connected to WiFi.");
  } else {
    Serial.println("\nConnection to new WiFi failed!");
    request->send(200, "text/plain", "Configuration failed. Unable to connect to WiFi.");
  }
}

String readStringFromEEPROM(int addr) {
  String value = "";
  char c;
  for (int i = addr; i < addr + 32; ++i) {
    c = EEPROM.read(i);
    if (c == 0) break;
    value += c;
  }
  Serial.println("EEPROM Read String: " + value); // Print the read value
  return value;
}

void writeStringToEEPROM(int addr, String value) {
  int len = value.length();
  for (int i = 0; i < len; ++i) {
    EEPROM.write(addr + i, value[i]);
  }
  EEPROM.write(addr + len, 0); // Null-terminate the string
}

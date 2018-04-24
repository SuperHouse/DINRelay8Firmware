/*
 * Control 8 relay outputs using buttons and MQTT messages. Connects to a WiFi
 * network, gets an address by DHCP, then subscribes to an MQTT broker and
 * awaits commands.
 * 
 * Uses an SSD1306 OLED to display the assigned IP address, the command topic,
 * and events.
 * 
 * MQTT command format is "output,state". Output 0 is all outputs. Eg:
 * 1,0 (output 1 off)
 * 3,1 (output 3 on)
 * 0,0 (all outputs off)
 * 0,1 (all outputs on)
 * 
 * Compile using the Arduino IDE with the ESP32 core installed, with the board
 * type set to "WEMOS LOLIN32"
 * 
 *  * Dependencies:
 * - "ESP8266 and ESP32 Oled Driver for SSD1306 display" (install through Arduino IDE library manager)
 *
 * Written by Jonathan Oxer for www.superhouse.tv
 * https://github.com/superhouse/DINRelay8Firmware
 */

/*    YOUR LOCAL CONFIGURATION                          */
#include "config.h"
/* Nothing below this point should require modification */

// Required libraries
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>       // For Arduino OTA updates
#include <ArduinoOTA.h>    // For Arduino OTA updates
#include "SSD1306Wire.h"   // For OLED
#include <PubSubClient.h>  // MQTT client

// General setup
long last_activity_time = 0;     // The millis() time that something last happened
int screen_timeout      = SCREENTIMEOUT * 1000;   // Configure this in "config.h"
int debounce_timeout    = 300;   // Milliseconds before a new button press will be recognised
uint64_t chipid;                 // Global to store the unique chip ID of this device

// WiFi setup
const char* ssid        = WIFISSID;       // Configure this in "config.h"
const char* password    = WIFIPASSWORD;   // Configure this in "config.h"

// MQTT setup
const char* mqtt_broker = MQTTBROKER;     // Configure this in "config.h"
char msg[75];              // General purpose  buffer for MQTT messages
char command_topic[50];    // Dynamically generated using the unique chip ID

// OLED setup
SSD1306Wire  display(0x3c, 21, 22);
String screen_line_1 = "";  // Line buffers for OLED
String screen_line_2 = "";
String screen_line_3 = "";
String screen_line_4 = "";
String screen_line_5 = "";
String screen_line_6 = "";

// Define pins
int output_pin[8] =   {14, 27, 26, 25, 19, 17, 18, 23};
int button_pin[8] =   {34, 35, 32, 33,  4, 16, 12,  2};
int output_state[8] = { 0,  0,  0,  0,  0,  0,  0,  0};
int button_state[8] = { 0,  0,  0,  0,  0,  0,  0,  0};

#define BUTTONUP 1
#define BUTTONDOWN 0

// Create objects for networking and MQTT
WiFiClient espClient;
PubSubClient client(espClient);

/*
 * Setup
 */
void setup() {
  // Set up the OLED screen
  initialise_screen();
  
  // Start output to screen
  screen_line_1 = "Starting DINRelay8  v1.0";
  screen_line_2 = "Connecting to network...";
  refresh_screen();
  
  // Start ouput to serial port
  Serial.begin(115200);
  Serial.println();
  Serial.println("=====================================");
  Serial.println("Starting up DINRelay8 v1.0");

  // Check and report on the flash memory on this board
  //uint32_t realSize = ESP.getFlashChipRealSize();
  //uint32_t ideSize = ESP.getFlashChipSize();
  //FlashMode_t ideMode = ESP.getFlashChipMode();
  //Serial.printf("Flash real id:   %08X\n", ESP.getFlashChipId());
  //Serial.printf("Flash real size: %u\n", realSize);
  //Serial.printf("Flash ide  size: %u\n", ideSize);
  //Serial.printf("Flash ide speed: %u\n", ESP.getFlashChipSpeed());
  //Serial.printf("Flash ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));
  //if(ideSize != realSize) {
  //  Serial.println("Flash Chip configuration wrong!\n");
  //} else {
  //  Serial.println("Flash Chip configuration ok.\n");
  //}

  // We need a unique device ID for our MQTT client connection
  chipid=ESP.getEfuseMac();  //The chip ID is essentially its MAC address(length: 6 bytes).
  //Serial.printf("ESP32 Chip ID = %04X",(uint16_t)(chipid>>32));  //print High 2 bytes
  Serial.printf("C: %08x\n",(uint32_t)chipid);  //print Low 4bytes.

  //sprintf(device_id, "%08X", chipid);
  //Serial.print("Device ID: ");
  //Serial.println(device_id);

  // Set up the topics for MQTT. By inserting the unique ID, the result is
  // of the form: "device/dab9616f/command"
  sprintf(command_topic, "device/%08x/command",  chipid);  // For receiving messages
  
  // Report the topics to the serial console and OLED
  Serial.print("Command topic:       ");
  Serial.println(command_topic);
  screen_line_3 = command_topic;
  refresh_screen();

  // Bring up the WiFi connection
  setup_wifi();

  // Prepare for OTA updates
  setup_ota();

  // Set up the MQTT client
  client.setServer(mqtt_broker, 1883);
  client.setCallback(mqtt_callback);

  // Make sure all the I/O pins are set up the way we need them
  enable_and_reset_all_outputs();
  enable_all_buttons();

  //Serial.print("IP address: ");
  //Serial.println(WiFi.localIP());

  // Report our setup
  /*
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    // print the value of each byte of the IP address:
    display.drawString(Ethernet.localIP()[thisByte], DEC);
    if( thisByte < 3 )
    {
      box.print(".");
    }
  } */

  screen_line_1 = "DINRelay8           v1.0";
  refresh_screen();
}

/*
 * Set up the OLED display
 */
void initialise_screen()
{
  display.init();
  //display.flipScreenVertically();
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
}

/*
 * Write all current line buffers to the screen
 */
void refresh_screen()
{
  last_activity_time = millis();
  display.clear();
  display.drawString(0,  0, screen_line_1);
  display.drawString(0, 10, screen_line_2);
  display.drawString(0, 20, screen_line_3);
  display.drawString(0, 30, screen_line_4);
  display.drawString(0, 40, screen_line_5);
  display.drawString(0, 50, screen_line_6);
  display.displayOn();
  display.display();
}

/*
 * Accept a text line for display to the screen, move all the line buffers 
 * up one position and remove the oldest, add the new line to the bottom,
 * and updated the display 
 */
void push_to_screen( char screenbuffer[25] )
{
  screen_line_4 = screen_line_5;
  screen_line_5 = screen_line_6;
  screen_line_6 = screenbuffer;
  refresh_screen();
}

/*
 * Set all the outputs to low
 */
void enable_and_reset_all_outputs()
{
  for(int i = 0; i < 8; i++)
  {
    pinMode(output_pin[i], OUTPUT);
    digitalWrite(output_pin[i], output_state[i]);
    output_state[i] = 0;
  }
}

/*
 * Set all the outputs to low
 */
void enable_all_buttons()
{
  gpio_pad_select_gpio( 36 );
  for(int i = 0; i < 8; i++)
  {
    pinMode(button_pin[i], INPUT);
  }
}

/*
 * Connect to a WiFi network and report the settings
 */
void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to SSID ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  screen_line_2 = WiFi.localIP().toString();
  refresh_screen();
}

/*
 * Callback invoked when an MQTT message is received.
 */
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  byte output_number = payload[0] - '0';
  byte output_state = payload[2] - '0';
  Serial.print("Output: ");
  Serial.println(output_number);
  Serial.print("State: ");
  Serial.println(output_state);

  switch(output_state)
  {
    case 0:
      turn_output_off(output_number);
      break;
    case 1:
      turn_output_on(output_number);
      break;
  }
}

/*
 * Set one or all outputs to off
 */
void turn_output_off( int output_number )
{
  char message[25];
  byte output_index = output_number - 1;

  if(output_number == 0)
  {
    for(int i=0; i<8; i++)
    {
      digitalWrite(output_pin[i], LOW);
      output_state[i] = 0;
    }
    sprintf(message, "Turning OFF all outputs");
  } else if(output_number < 9) {
    digitalWrite(output_pin[output_index], LOW);
    output_state[output_index] = 0;
    sprintf(message, "Turning OFF output %d", output_number);
  }
  
  Serial.println(message);
  push_to_screen( message );
}

/*
 * Set one or all outputs to on
 */
void turn_output_on( int output_number )
{
  char message[25];
  byte output_index = output_number - 1;
  
  if(output_number == 0)
  {
    for(int i=0; i<8; i++)
    {
      digitalWrite(output_pin[i], HIGH);
      output_state[output_index] = 1;
    }
    sprintf(message, "Turning ON all outputs");
  } else if(output_number < 9) {
    digitalWrite(output_pin[output_index], HIGH);
    output_state[output_index] = 1;
    sprintf(message, "Turning ON output %d", output_number);
  }
  
  Serial.println(message);
  push_to_screen( message );
}

/*
 * Repeatedly attempt connection to MQTT broker until we succeed. Or until the heat death of
 * the universe, whichever comes first
 */
void reconnect() {
  
  char mqtt_client_id[20];
  sprintf(mqtt_client_id, "esp32-%08x", chipid);
  
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(mqtt_client_id)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      sprintf(msg, "Device %s starting up", mqtt_client_id);
      client.publish("events", msg);
      // ... and resubscribe
      client.subscribe(command_topic);
      Serial.print("Subscribing to ");
      Serial.println(command_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

/*
 * OTA updates from the Arduino IDE
 */
void setup_ota()
{
  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  // ArduinoOTA.setHostname("myesp32");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with its md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();
}

/*
 * Check if any buttons are pressed
 */
void scan_buttons()
{
  if(millis() - last_activity_time > debounce_timeout)
  {
    byte this_button_state = BUTTONUP;
    for(int i=0; i<8; i++)
    {
      this_button_state = digitalRead(button_pin[i]);
      //Serial.print(this_button_state);
      if(this_button_state == BUTTONDOWN && button_state[i] == BUTTONUP)
      {
        // We've detected a keypress
        last_activity_time = millis();
        screen_line_4 = "Button press";
        refresh_screen();
        char button[2];
        sprintf(button, "%d", i);
        button_state[i] = BUTTONDOWN;
        byte output_number = i + 1;
        if(output_state[i] == 0)
        {
          turn_output_on(output_number);
        } else {
          turn_output_off(output_number);
        }
      }
  
      button_state[i] = this_button_state;
    }
  }
}

/*
 * =========================================================================
 * Main program loop
 */
void loop() {
  // Handle OTA update requests
  ArduinoOTA.handle();

  // Always try to keep the connection to the MQTT broker alive
  if (!client.connected()) {
    reconnect();
  }

  // Handle any MQTT tasks
  client.loop();

  // Check if any buttons are pressed
  scan_buttons();

  // Task scheduling
  unsigned long time_now = millis();

  // Handle screensaver
  if (time_now >= last_activity_time + screen_timeout) {
    display.displayOff();
  }
}

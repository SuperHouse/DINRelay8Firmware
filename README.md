DIN Relay8 Controller Firmware
==============================

Control 8 relay outputs using buttons and MQTT messages. Connects to a
WiFi network, gets an address by DHCP, then subscribes to an MQTT
broker and awaits commands.

Uses an SSD1306 OLED to display the assigned IP address, the command
topic, and events.

MQTT command format is "output,state". Output 0 is all outputs. Eg:

 * 1,0 (output 1 off)
 * 3,1 (output 3 on)
 * 0,0 (all outputs off)
 * 0,1 (all outputs on)

Compile using the Arduino IDE with the ESP32 core installed, with the
board type set to "WEMOS LOLIN32"

== Dependencies ==
 * "ESP8266 and ESP32 Oled Driver for SSD1306 display" (install through Arduino IDE library manager)

Written by Jonathan Oxer for www.superhouse.tv


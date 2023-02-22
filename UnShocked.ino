#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include "Arduino.h"
#include "esp32-hal.h"
#include <vector>
#include <bitset>
#include <numeric>
#include <map>
#include <TaskScheduler.h>


const char* ssid     = "Luc-H";         // The SSID (name) of the Wi-Fi network you want to connect to
const char* password = "LucNetworkPw12";     // The password of the Wi-Fi network

WiFiMulti WiFiMulti;
WebSocketsClient webSocket;
TaskHandle_t Task1;

#define USE_SERIAL Serial

struct command_t {
  uint shockerId;
  uint method;
  uint intensity;
  uint duration;
  std::vector<rmt_data_t> sequence;
  ulong until;
};

std::map<uint, command_t> Commands;

void hexdump(const void *mem, uint32_t len, uint8_t cols = 16) {
	const uint8_t* src = (const uint8_t*) mem;
	USE_SERIAL.printf("\n[HEXDUMP] Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
	for(uint32_t i = 0; i < len; i++) {
		if(i % cols == 0) {
			USE_SERIAL.printf("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
		}
		USE_SERIAL.printf("%02X ", *src);
		src++;
	}
	USE_SERIAL.printf("\n");
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {


    switch(type) {
        case WStype_DISCONNECTED:
            USE_SERIAL.printf("[WSc] Disconnected!\n");
            break;
        case WStype_CONNECTED:
            {
                USE_SERIAL.printf("[WSc] Connected to url: %s\n",  payload);

			    // send message to server when Connected
				webSocket.sendTXT("Connected");
            }
            break;
        case WStype_TEXT:
            USE_SERIAL.printf("[WSc] get text: %s\n", payload);
            //IntakeCommand(3068, 2, 50, 5000);
            //IntakeCommand(3045, 2, 50, 2500);
            IntakeCommand(999999, 2, 50, 2500);

			// send message to server
			// webSocket.sendTXT("message here");
            break;
        case WStype_BIN:
            USE_SERIAL.printf("[WSc] get binary length: %u\n", length);
            hexdump(payload, length);

            // send data to server
            // webSocket.sendBIN(payload, length);
            break;
		case WStype_ERROR:			
		case WStype_FRAGMENT_TEXT_START:
		case WStype_FRAGMENT_BIN_START:
		case WStype_FRAGMENT:
		case WStype_FRAGMENT_FIN:
			break;
    }

}




rmt_obj_t* rmt_send = NULL;

void Task1code( void * parameter) {
    Serial.print("Task2 running on core ");
    Serial.println(xPortGetCoreID());
    while(true) {
        RmtLoop();
    }
}

void setup() {
    // USE_SERIAL.begin(921600);
    USE_SERIAL.begin(115200);

    //Serial.setDebugOutput(true);
    USE_SERIAL.setDebugOutput(true);

    USE_SERIAL.println();
    USE_SERIAL.println();
    USE_SERIAL.println();

        Serial.println(millis());

      for(uint8_t t = 4; t > 0; t--) {
          USE_SERIAL.printf("[SETUP] BOOT WAIT %d...\n", t);
          USE_SERIAL.flush();
          delay(1000);
      }

    xTaskCreate(
          Task1code, /* Function to implement the task */
          "Task1", /* Name of the task */
          10000,  /* Stack size in words */
          NULL,  /* Task input parameter */
          0,  /* Priority of the task */
          &Task1  /* Task handle. */);

    WiFiMulti.addAP(ssid, password);

    //WiFi.disconnect();
    while(WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
    }


    if ((rmt_send = rmtInit(13, RMT_TX_MODE, RMT_MEM_64)) == NULL)
    {
        Serial.println("init sender failed\n");
    }

    float realTick = rmtSetTick(rmt_send, 1000);
    Serial.printf("real tick set to: %fns\n", realTick);

  	

    webSocket.beginSSL("10.0.0.4", 443, "/test");
    webSocket.onEvent(webSocketEvent);
    

}

void loop() {
    webSocket.loop();
}

rmt_data_t startBit = {
  1400,
  1,
  800,
  0
};

rmt_data_t oneBit = {
    800,
    1,
    300,
    0
};

rmt_data_t zeroBit = {
    300,
    1,
    800,
    0
};

std::vector<rmt_data_t> to_rmt_data(const std::vector<uint8_t>& data) {
    std::vector<rmt_data_t> pulses;

    pulses.push_back(startBit);

    for (auto byte : data) {
      std::bitset<8> bits(byte);
        for (int bit_pos = 7; bit_pos >= 0; --bit_pos) {

          if(bits[bit_pos]) {
            pulses.push_back(oneBit);
          } else {
            pulses.push_back(zeroBit);
          }
        }
    }

    for (int i = 0; i < 3; ++i) {
        pulses.push_back(zeroBit);
    }

    return pulses;
}

std::vector<rmt_data_t> GetSequence(unsigned int shockerId, unsigned int method, unsigned int intensity) {
    std::vector<uint8_t> data = {
        (shockerId & 0xFF00) >> 8,
        shockerId & 0xFF,
        method,
        intensity
    };

    int checksum = std::accumulate(data.begin(), data.end(), 0) & 0xff;
    data.push_back(checksum);

    return to_rmt_data(data);
}

void IntakeCommand(uint shockerId, uint method, uint intensity, uint duration) {
    if(Commands.count(shockerId) > 0) {
      // FIXME: Callback in websocket to informat about busy shocker.
      return;
    }

    std::vector<rmt_data_t> rmtData = GetSequence(shockerId, method, intensity);

    command_t cmd = {
      shockerId,
      method,
      intensity,
      duration,
      rmtData,
      millis() + duration
    };
    Commands[shockerId] = cmd;

}

void RmtLoop() {
    if(Commands.size() <= 0) {
      return;
    }

    std::vector<rmt_data_t> sequence;

    Serial.println(Commands.size());

    for (auto& it : Commands) {

        if(it.second.until <= millis()) {
          Commands.erase(it.first);
          continue;
        }
        sequence.insert(sequence.end(), it.second.sequence.begin(), it.second.sequence.end());
    }

    std::size_t finalSize = sequence.size();

    if(finalSize <= 0) {
      Serial.print("Return with ");
      Serial.println(finalSize);
      return;
    }

    Serial.print(finalSize);

    rmt_data_t* arr = new rmt_data_t[finalSize];
    std::copy(sequence.begin(), sequence.end(), arr);
    rmtWriteBlocking(rmt_send, arr, finalSize);
    delete[] arr;
}

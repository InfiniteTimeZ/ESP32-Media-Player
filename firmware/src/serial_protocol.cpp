#include <Arduino.h>
#include "serial_protocol.h"
#include "discord_ui.h"
#include "media_display.h"



#define PACKET_MAGIC_1 0xA5
#define PACKET_MAGIC_2 0x5A
#define MAX_IMAGE_PAYLOAD 60000
#define MAX_PACKET_PAYLOAD (MAX_IMAGE_PAYLOAD + 1)

enum SerialState {
  FIND_MAGIC_1, 
  FIND_MAGIC_2, 
  READ_PACKET_TYPE, 
  READ_PACKET_LENGTH, 
  READ_PACKET_PAYLOAD
}; 

static SerialState serial_state = FIND_MAGIC_1;
 
static uint32_t last_byte_time = 0;
static uint32_t discarded_bytes = 0;

static uint8_t packet_type = 0;
static uint8_t packet_length_bytes[4];
static uint8_t packet_length_index = 0;

static uint32_t expected_payload_length = 0;
static uint32_t payload_bytes_read = 0;

static uint8_t* shared_packet_buffer = nullptr;


static String payload_to_string(const uint8_t* payload, uint32_t length){
  String result;
  result.reserve(length);

  for(uint32_t i = 0; i < length; i++){
    result += (char)payload[i];
  }

  return result;
}



static bool is_valid_packet_type(uint8_t type){
  switch (type){
    case 'R':
    case 'T':
    case 'I':
    case 'U':
    case 'A':
    case 'D':
    case 'N':
      return true;
    
    default:
      return false;
  }
}

static void process_packet(uint8_t type, const uint8_t* payload, uint32_t length){
  switch(type){
    case 'R':
      if (length ==0){
        Serial.println("ESP32_READY");
      }
      break;

    case 'T': {
      String json = payload_to_string(payload, length);
      process_track_json(json);
      break;
    }

    case 'I': {
      if(length == 0 || length > MAX_IMAGE_PAYLOAD){
        Serial.printf("Invalid album length: %lu\n", (unsigned long)length);
        return;
      }

      process_album_art(payload, length);
      break;
    }

    case 'U': {
      String user_json = payload_to_string(payload, length);
      process_voice_users_json(user_json);
      break;
    }

    case 'A': {
      if(length < 2){
        Serial.println("Invalid avatar packet");
        return;
      }

      uint8_t user_index = payload[0];
      const uint8_t* jpeg_data = &payload[1];
      uint32_t jpeg_length = length - 1;

      if(jpeg_length > MAX_IMAGE_PAYLOAD){
        Serial.println("Avatar image exceeds maximum size");
        return;
      }

      process_avatar_message(user_index, jpeg_data, jpeg_length);
      break;
    }

    case 'D':
      process_discord_state_packet(payload, length);
      break;

    case 'N': {
      set_voice_channel_name(payload, length);
      break;
    }

    default:
      Serial.printf("Unknown packet type: 0x%02X\n", type);
      break;
  }
}

static void reset_serial_parser(){
  serial_state = FIND_MAGIC_1;

  packet_type = 0;
  packet_length_index = 0;
  expected_payload_length = 0;
  payload_bytes_read = 0;

}


static void init_packet_buffer(){
  shared_packet_buffer = (uint8_t*)heap_caps_malloc(MAX_PACKET_PAYLOAD, MALLOC_CAP_SPIRAM);
  if (shared_packet_buffer == nullptr) {
        Serial.println("Failed to allocate serial packet buffer");
        abort();
  }
}


void init_serial_protocol(){
  init_packet_buffer();
}

void handle_serial_input() {
  if (serial_state != FIND_MAGIC_1 && (millis() - last_byte_time > 1500)) {
    Serial.println("Serial stall detected! Stream desynced. Flushing trash...");
    reset_serial_parser();
  }

  while (Serial.available() > 0) {
    last_byte_time = millis();

    if (serial_state == READ_PACKET_PAYLOAD) {
      uint32_t remaining = expected_payload_length - payload_bytes_read;
      uint32_t available = Serial.available();
      uint32_t bytes_to_read = min(remaining, available);
      
      if (bytes_to_read > 0){
        int bytes_read = Serial.readBytes((char*)&shared_packet_buffer[payload_bytes_read], bytes_to_read);
        payload_bytes_read += bytes_read;
      }

      if (payload_bytes_read >= expected_payload_length){
        process_packet(packet_type, shared_packet_buffer, expected_payload_length);
        reset_serial_parser();
      }
      continue;
    }
    
    uint8_t byte = Serial.read();

    switch (serial_state) {
      case FIND_MAGIC_1:
        if(byte == PACKET_MAGIC_1){
          if(discarded_bytes > 0){
            Serial.printf("Serial resync after discarding %lu bytes\n", (unsigned long)discarded_bytes);
            discarded_bytes = 0;
          }

          serial_state = FIND_MAGIC_2;

        }else{
          discarded_bytes++;
        }
        break;
      
      case FIND_MAGIC_2:
        if(byte == PACKET_MAGIC_2){
          serial_state = READ_PACKET_TYPE;
        } 
        else if(byte == PACKET_MAGIC_1){
          serial_state = FIND_MAGIC_2;
        }
        else{
          serial_state = FIND_MAGIC_1;
        }
        break;

      case READ_PACKET_TYPE:
        if (!is_valid_packet_type(byte)){
          Serial.printf("Invalid packet type: 0x%02X\n", byte);
          reset_serial_parser();
          break;
         }

         packet_type = byte;
         packet_length_index = 0;
         expected_payload_length = 0;
         serial_state = READ_PACKET_LENGTH;

         break;

      case READ_PACKET_LENGTH:
        packet_length_bytes[packet_length_index++] = byte;

        if(packet_length_index == 4){
          expected_payload_length = (uint32_t)packet_length_bytes[0] | ((uint32_t)packet_length_bytes[1] << 8) | ((uint32_t)packet_length_bytes[2] << 16) | ((uint32_t)packet_length_bytes[3] << 24);

          if (expected_payload_length > MAX_PACKET_PAYLOAD){
            Serial.printf("Invalid packet length: %lu\n", (unsigned long)expected_payload_length);
            reset_serial_parser();
            break;
          }

          payload_bytes_read = 0;

          if (expected_payload_length == 0){
            process_packet(packet_type, shared_packet_buffer, 0);
            reset_serial_parser();
          }
          else{
            serial_state = READ_PACKET_PAYLOAD;
          }
        }
        break;

      case READ_PACKET_PAYLOAD:
        break;
       
    }
  }
}



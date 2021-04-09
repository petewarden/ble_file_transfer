/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include <ArduinoBLE.h>

#define FILE_TRANSFER_UUID(val) ("bf88b656-" val "-4a61-86e0-769c741026c0")

namespace {
  
BLEService service(FILE_TRANSFER_UUID("0000"));

constexpr int32_t file_block_byte_count = 512;
BLECharacteristic file_block_characteristic(FILE_TRANSFER_UUID("3000"), BLEWrite, file_block_byte_count);

BLECharacteristic file_length_characteristic(FILE_TRANSFER_UUID("3001"), BLERead | BLEWrite, sizeof(uint32_t));

BLECharacteristic file_maximum_length_characteristic(FILE_TRANSFER_UUID("3002"), BLERead, sizeof(uint32_t));
BLECharacteristic file_checksum_characteristic(FILE_TRANSFER_UUID("3003"), BLERead | BLEWrite, sizeof(uint32_t));
BLECharacteristic command_characteristic(FILE_TRANSFER_UUID("3004"), BLEWrite, sizeof(uint32_t));
BLECharacteristic error_status_characteristic(FILE_TRANSFER_UUID("3005"), BLERead, sizeof(uint32_t));

constexpr int32_t error_message_byte_count = 128;
BLECharacteristic error_message_characteristic(FILE_TRANSFER_UUID("3006"), BLERead, error_message_byte_count);

String device_name;

constexpr int32_t file_maximum_byte_count = (50 * 1024);
uint8_t file_buffers[2][file_maximum_byte_count];
int finished_file_buffer_index = -1;
uint8_t* finished_file_buffer = nullptr;
int32_t finished_file_buffer_byte_count = 0;

uint8_t* in_progress_file_buffer = nullptr;
int32_t in_progress_bytes_received = 0;
int32_t in_progress_bytes_expected = 0;
uint32_t in_progress_checksum = 0;

void setError(const String& error_message) {
  Serial.println(error_message);
  constexpr int32_t error_status_code = 1;
  error_status_characteristic.writeValue(error_status_code);
 
  const char* error_message_bytes = error_message.c_str();
  uint8_t error_message_buffer[error_message_byte_count];
  bool at_string_end = false;
  for (int i = 0; i < error_message_byte_count; ++i) {
    const bool at_last_byte = (i == (error_message_byte_count - 1));
    if (!at_string_end && !at_last_byte) {
      const char current_char = error_message_bytes[i];
      if (current_char == 0) {
        at_string_end = true;
      } else {
        error_message_buffer[i] = current_char;
      }
    }

    if (at_string_end || at_last_byte) {
      error_message_buffer[i] = 0;
    }
  }
  error_message_characteristic.writeValue(error_message_buffer, error_message_byte_count);
}

uint32_t computeChecksum() {
  return 0;
}

void onFileTransferComplete() {
  uint32_t computed_checksum = computeChecksum();
  if (in_progress_checksum != computed_checksum) {
    setError(String("File transfer failed: Expected checksum 0x") + String(in_progress_checksum, 16) + 
      String(" but received 0x") + String(computed_checksum, 16));
    in_progress_file_buffer = nullptr;
    return;
  }

  if (finished_file_buffer_index == 0) {
    finished_file_buffer_index = 1;
  } else {
    finished_file_buffer_index = 0;
  }
  finished_file_buffer = &file_buffers[finished_file_buffer_index][0];;
  finished_file_buffer_byte_count = in_progress_bytes_expected;

  in_progress_file_buffer = nullptr;
  in_progress_bytes_received = 0;
  in_progress_bytes_expected = 0;
}

void onFileBlockWritten(BLEDevice central, BLECharacteristic characteristic) {  
  if (in_progress_file_buffer == nullptr) {
    setError("File block sent while no valid command is active");
    return;
  }
  
  const int32_t file_block_length = characteristic.valueLength();
  if (file_block_length > file_block_byte_count) {
    setError(String("Too many bytes in block: Expected ") + String(file_block_byte_count) + 
      String(" but received ") + String(file_block_length));
    in_progress_file_buffer = nullptr;
    return;
  }
  
  const int32_t bytes_received_after_block = in_progress_bytes_received + file_block_length;
  if ((bytes_received_after_block > in_progress_bytes_expected) ||
    (bytes_received_after_block > file_maximum_byte_count)) {
    setError(String("Too many bytes: Expected ") + String(in_progress_bytes_expected) + 
      String(" but received ") + String(bytes_received_after_block));
    in_progress_file_buffer = nullptr;
    return;
  }
  
  Serial.print("Data received: length = ");
  Serial.println(file_block_length);

  uint8_t* file_block_buffer = in_progress_file_buffer + in_progress_bytes_received;
  characteristic.readValue(file_block_buffer, file_block_length);

  char string_buffer[file_block_byte_count + 1];
  for (int i = 0; i < file_block_length; ++i) {
    unsigned char value = file_block_buffer[i];
    if (i < file_block_byte_count) {
      string_buffer[i] = value;
    }
  }
  string_buffer[file_block_byte_count] = 0;
  Serial.println(String(string_buffer));

  if (bytes_received_after_block == in_progress_bytes_expected) {
    onFileTransferComplete();
  } else {
    in_progress_bytes_received = bytes_received_after_block;    
  }
}

void onCommandWritten(BLEDevice central, BLECharacteristic characteristic) {
  if (in_progress_file_buffer != nullptr) {
    setError("File transfer command received while previous transfer is still in progress");
    return;
  }
  
  int32_t command_value;
  characteristic.readValue(command_value);

  if (command_value != 1) {
    setError(String("Bad command value: Expected 1 but received ") + String(command_value));
    return;
  }

  int32_t file_length_value; 
  file_length_characteristic.readValue(file_length_value);
  if (file_length_value > file_maximum_byte_count) {
    setError(
       String("File too large: Maximum is ") + String(file_maximum_byte_count) + 
       String(" bytes but request is ") + String(file_length_value) + String(" bytes"));
    return;
  }

  file_checksum_characteristic.readValue(in_progress_checksum);

  int in_progress_file_buffer_index;
  if (finished_file_buffer_index == 0) {
    in_progress_file_buffer_index = 1;
  } else {
    in_progress_file_buffer_index = 0;
  }
  
  in_progress_file_buffer = &file_buffers[in_progress_file_buffer_index][0];
  in_progress_bytes_received = 0;
  in_progress_bytes_expected = file_length_value;
}

}  // namespace

void setup() {
  // Start serial
  Serial.begin(9600);
  Serial.println("Started");

  // Start BLE
  if (!BLE.begin()) {
    Serial.println("Failed to initialized BLE!");
    while (1);
  }
  String address = BLE.address();

  // Output BLE settings over Serial
  Serial.print("address = ");
  Serial.println(address);

  address.toUpperCase();

  device_name = "FileTransferExample-";
  device_name += address[address.length() - 5];
  device_name += address[address.length() - 4];
  device_name += address[address.length() - 2];
  device_name += address[address.length() - 1];

  Serial.print("device_name = ");
  Serial.println(device_name);

  BLE.setLocalName(device_name.c_str());
  BLE.setDeviceName(device_name.c_str());
  BLE.setAdvertisedService(service);

  file_block_characteristic.setEventHandler(BLEWritten, onFileBlockWritten);
  service.addCharacteristic(file_block_characteristic);

  service.addCharacteristic(file_length_characteristic);

  file_maximum_length_characteristic.writeValue(file_maximum_byte_count);
  service.addCharacteristic(file_maximum_length_characteristic);

  service.addCharacteristic(file_checksum_characteristic);

  command_characteristic.setEventHandler(BLEWritten, onCommandWritten);
  service.addCharacteristic(command_characteristic);

  constexpr int32_t success_error_code = 0;
  error_status_characteristic.writeValue(success_error_code);
  service.addCharacteristic(error_status_characteristic);

  service.addCharacteristic(error_message_characteristic);

  BLE.addService(service);
  BLE.advertise();
}

void loop() {
  BLEDevice central = BLE.central();
  
  // if a central is connected to the peripheral:
  static bool was_connected_last = false;  
  if (central && !was_connected_last) {
    Serial.print("Connected to central: ");
    // print the central's BT address:
    Serial.println(central.address());
  }
  was_connected_last = central;  

}

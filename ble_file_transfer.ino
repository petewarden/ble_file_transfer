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

constexpr int32_t file_block_byte_count = 512;
uint8_t file_block_buffer[file_block_byte_count] = {};
  
const int VERSION = 0x00000000;
BLEService service(FILE_TRANSFER_UUID("0000"));
BLECharacteristic file_block_characteristic(FILE_TRANSFER_UUID("3000"), BLEWrite, file_block_byte_count);
String name;

void on_file_block_written(BLEDevice central, BLECharacteristic characteristic) {
  const int32_t file_block_length = characteristic.valueLength();
//  Serial.print("Data received: length = ");
//  Serial.println(file_block_length);
  
  characteristic.readValue(file_block_buffer, file_block_length);

  char string_buffer[file_block_byte_count + 1];
  for (int i = 0; i < file_block_length; ++i) {
    unsigned char value = file_block_buffer[i];
//    Serial.print(value);
//    Serial.print(',');
    if (i < file_block_byte_count) {
      string_buffer[i] = value;
    }
  }
  string_buffer[file_block_byte_count] = 0;
//  Serial.println("");
//  Serial.println(String(string_buffer));
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

  name = "FileTransferExample-";
  name += address[address.length() - 5];
  name += address[address.length() - 4];
  name += address[address.length() - 2];
  name += address[address.length() - 1];

  Serial.print("name = ");
  Serial.println(name);

  BLE.setLocalName(name.c_str());
  BLE.setDeviceName(name.c_str());
  BLE.setAdvertisedService(service);

  file_block_characteristic.setEventHandler(BLEWritten, on_file_block_written);
  service.addCharacteristic(file_block_characteristic);

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

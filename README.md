# BLE File Transfer
Example of transferring file data over BLE to an Arduino Nano Sense using WebBLE.

## Overview

This is an example of how to use Bluetooth Low Energy to transfer small files (in the tens of kilobytes range) from a client to a device like an Arduino Nano Sense. The BLE protocol isn't designed for sending more than a few bytes at a time, so I've had to put together an approach layered on top of the core API.

There are a lot of restrictions to be aware of:
 - The code is only lightly tested so far, so I expect there will be some cases it doesn't handle.
 - Data transfer speeds are only a few kilobytes per second.
 - It only handles writing data from a client to a BLE device, not the other way around.

## Trying it out

To begin, flash the ble_file_transfer.ino sketch onto an Arduino Nano BLE Sense 33 board, open the serial monitor in the Arduino IDE, and then navigate to [https://petewarden.github.io/ble_file_transfer/website/index.html](https://petewarden.github.io/ble_file_transfer/website/index.html) (or your own copy if you've forked the repo).

That page should give you instructions to test the code, beginning with connecting to the board and then transfering a file.

## How to use this code for your own application

To transfer files you need a client, in this case a web page running in a browser like Chrome, and a device to receive the data, which we'll use an Arduino board for. Both places need the appropriate code running to hand over the file data successfully, so I'll describe what you need to do on both.

### On the Arduino

This code has been tested with the Arduino Nano Sense 33 BLE board, and you'll find the sketch in this folder as ble_file_transfer.ino. If you load this source file, you'll see a lot of implementation code at the top, but if you scroll down there are only a few functions you need to worry about:

```c++
void setup() {
  // Start serial
  Serial.begin(9600);
  Serial.println("Started");

  setupBLEFileTransfer();
}

void onBLEFileReceived(uint8_t* file_data, int file_length) {
  // Do something here with the file data that you've received. The memory itself will
  // remain untouched until after a following onFileReceived call has completed, and
  // the BLE module retains ownership of it, so you don't need to deallocate it.
}

void loop() {
  updateBLEFileTransfer();
  // Your own code here.
}
```

You have to call setupBLEFileTransfer in your setup function to start the file transfer service, and then call updateBLEFileTransfer every loop in order to give it a chance to handle requests.

When a file has been completed, the onBLEFileReceived function will be called. Once you've tested that the basic code works together with the test web page discussed later, you should modify that function so that it does what you want when a file has been passed to your board.

The only other part of the API you might want to change is the maximum file size, since we have to keep buffers in RAM to store the files. This is controlled by a constant near the top of the sketch.

```c++
// Controls how large a file the board can receive. We double-buffer the files
// as they come in, so you'll need twice this amount of RAM. The default is set
// to 50KB.
constexpr int32_t file_maximum_byte_count = (50 * 1024);
```

### On the client

We're using WebBLE on Chrome through a web page to test this example, but it could be any BLE-compatible client.

Inlined as a script within the website/index.html page in this repository you'll find a BLE implementation of the file transfer protocol from the client side. There are a lot of implementation details, but at the top you'll find the main API calls you'll need to make:

```Javascript
connectButton.addEventListener('click', function(event) {
  connect();
  transferFileButton.addEventListener('click', function(event) {
    msg('Trying to write file ...');
    // You'll want to replace this with the data you want to transfer.
    let fileContents = prepareDummyFileContents(30 * 1024);
    transferFile(fileContents);
  });
  cancelTransferButton.addEventListener('click', function(event) {
    msg('Trying to cancel transfer ...');
    cancelTransfer();
  });
});
```

You first need to call connect to ask the user to pair with your Arduino board, and then transferFile will start the sending process. The rest of the API are callbacks that happen when events are triggered, such as:

```Javascript
// You'll want to replace these two functions with your own logic, to take what
// actions your application needs when a file transfer succeeds, or errors out.
async function onTransferSuccess() {
  isFileTransferInProgress = false;
  let checksumValue = await fileChecksumCharacteristic.readValue();
  let checksumArray = new Uint32Array(checksumValue.buffer);
  let checksum = checksumArray[0];
  msg('File transfer succeeded: Checksum 0x' + checksum.toString(16));
}

// Called when something has gone wrong with a file transfer.
function onTransferError() {
  isFileTransferInProgress = false;
  msg("File transfer error");  
}

// Called when an error message is received from the device. This describes what
// went wrong with the transfer in a user-readable form.
function onErrorMessageChanged(event) {
  let value = new Uint8Array(event.target.value.buffer);
  let utf8Decoder = new TextDecoder();
  let errorMessage = utf8Decoder.decode(value);
  console.log("Error message = " + errorMessage);
}
```

You should modify the contents of these callback functions to do what you need in your application. Note that if you try to send a file that's too large, or when a file is already in progress, you'll get an error.

## How does the protocol work?

The file transfer works by having the client write blocks of bytes to a characteristic on the device, with the receiver assembling those ordered blocks back into a complete file.

A more detailed flow is:
 - A client pairs with the device, looking for the service with the UUID of `bf88b656-0000-4a61-86e0-769c741026c0`. 
 - When the client has a file to send, it first writes the file length and CRC32 checksum to characteristics on the device.
 - Then it starts a file transfer by writing an integer of 1 to the command characteristic.
 - The device then expects the client to repeatedly write sequential blocks of 128 bytes or less to the file block characteristic, waiting until one has been acknowledged before sending the next.
 - The client assembles these blocks into a contiguous array of data.
 - Once the expected number of bytes has been received, the device confirms the checksum matches the one supplied by the client, and then calls the onBLEFileReceived function with the received data.
 - The client is notified that the file transfer succeeded through a notification of the transfer code as 0.
 
If there's an error on the device side, then the client is sent an error status through a notification on the transfer code characteristic, with the number set to 1. A notification is also sent when a file transfer starts, indicated with a status code of 2.

It's possible to cancel an in-progress transfer if the client writes a value of 2 to the command characteristic. The device should then notify the client of an error if there were any transfers occurring.

## Known issues

 - The maximum block size is set to 128 bytes, since going over that seems to affect the reliability of the connection. We should be able to get up to 512 bytes theoretically, but I don't know why this doesn't work.
 - Chrome on Android supports WebBLE, but the transfer speeds seem very slow compared to Chrome desktop.
 - There are almost certainly a lot of tricky race conditions, security holes and other edge cases this protocol doesn't handle.
 - Related to the above, there are very few tests in this initial version.

## Thanks

This code wouldn't be possible without Dominic Pajak, Sandeep Mistry, and many of the other Google and Arduino people who've helped!

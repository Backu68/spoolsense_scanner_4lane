// NAME: PN5180ISO14443.cpp
//
// DESC: ISO14443 protocol on NXP Semiconductors PN5180 module for Arduino.
//
// Copyright (c) 2019 by Dirk Carstensen. All rights reserved.
//
// This file is part of the PN5180 library for the Arduino environment.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// Lesser General Public License for more details.
//
// Source: https://github.com/ATrappmann/PN5180-Library
// Modified: added SCKpin/MISOpin/MOSIpin constructor parameters for ESP32 custom SPI wiring.
//
// #define DEBUG 1

#include <Arduino.h>
#include "PN5180ISO14443.h"
#include <PN5180.h>
#include "Debug.h"

PN5180ISO14443::PN5180ISO14443(uint8_t SSpin, uint8_t BUSYpin, uint8_t RSTpin,
                               int8_t SCKpin, int8_t MISOpin, int8_t MOSIpin)
              : PN5180(SSpin, BUSYpin, RSTpin, SCKpin, MISOpin, MOSIpin) {
}

bool PN5180ISO14443::setupRF() {
  PN5180DEBUG(F("Loading RF-Configuration...\n"));
  if (loadRFConfig(0x00, 0x80)) {  // ISO14443 parameters
    PN5180DEBUG(F("done.\n"));
  }
  else return false;

  PN5180DEBUG(F("Turning ON RF field...\n"));
  if (setRF_on()) {
    PN5180DEBUG(F("done.\n"));
  }
  else return false;

  return true;
}

uint16_t PN5180ISO14443::rxBytesReceived() {
	uint32_t rxStatus;
	uint16_t len = 0;
	readRegister(RX_STATUS, &rxStatus);
	// Lower 9 bits has length
	len = (uint16_t)(rxStatus & 0x000001ff);
	return len;
}
/*
* buffer : must be 10 byte array
* buffer[0-1] is ATQA
* buffer[2] is sak
* buffer[3..6] is 4 byte UID
* buffer[7..9] is remaining 3 bytes of UID for 7 Byte UID tags
* kind : 0  we send REQA, 1 we send WUPA
*
* return value: the uid length:
* -	zero if no tag was recognized
* -	single Size UID (4 byte)
* -	double Size UID (7 byte)
* -	triple Size UID (10 byte) - not yet supported
*/
uint8_t PN5180ISO14443::activateTypeA(uint8_t *buffer, uint8_t kind) {
	uint8_t cmd[7];
	uint8_t uidLength = 0;
	// Load standard TypeA protocol
	if (!loadRFConfig(0x0, 0x80))
	  return 0;

	// OFF Crypto
	if (!writeRegisterWithAndMask(SYSTEM_CONFIG, 0xFFFFFFBF))
	  return 0;
	// Clear RX CRC
	if (!writeRegisterWithAndMask(CRC_RX_CONFIG, 0xFFFFFFFE))
	  return 0;
	// Clear TX CRC
	if (!writeRegisterWithAndMask(CRC_TX_CONFIG, 0xFFFFFFFE))
	  return 0;
	//Send REQA/WUPA, 7 bits in last byte
	cmd[0] = (kind == 0) ? 0x26 : 0x52;
	if (!sendData(cmd, 1, 0x07))
	  return 0;
	// READ 2 bytes ATQA into  buffer
	if (!readData(2, buffer))
	  return 0;
	//Send Anti collision 1, 8 bits in last byte
	cmd[0] = 0x93;
	cmd[1] = 0x20;
	if (!sendData(cmd, 2, 0x00))
	  return 0;
	//Read 5 bytes, we will store at offset 2 for later usage
	if (!readData(5, cmd+2))
	  return 0;
	//Enable RX CRC calculation
	if (!writeRegisterWithOrMask(CRC_RX_CONFIG, 0x01))
	  return 0;
	//Enable TX CRC calculation
	if (!writeRegisterWithOrMask(CRC_TX_CONFIG, 0x01))
	  return 0;
	//Send Select anti collision 1, the remaining bytes are already in offset 2 onwards
	cmd[0] = 0x93;
	cmd[1] = 0x70;
	if (!sendData(cmd, 7, 0x00))
	  return 0;
	//Read 1 byte SAK into buffer[2]
	if (!readData(1, buffer+2))
	  return 0;
	// Check if the tag is 4 Byte UID or 7 byte UID and requires anti collision 2
	// If Bit 3 is 0 it is 4 Byte UID
	if ((buffer[2] & 0x04) == 0) {
		// Take first 4 bytes of anti collision as UID store at offset 3 onwards. job done
		for (int i = 0; i < 4; i++) buffer[3+i] = cmd[2 + i];
		uidLength = 4;
	}
	else {
		// Take First 3 bytes of UID, Ignore first byte 88(CT)
		if (cmd[2] != 0x88)
		  return 0;
		for (int i = 0; i < 3; i++) buffer[3+i] = cmd[3 + i];
		// Clear RX CRC
		if (!writeRegisterWithAndMask(CRC_RX_CONFIG, 0xFFFFFFFE))
	      return 0;
		// Clear TX CRC
		if (!writeRegisterWithAndMask(CRC_TX_CONFIG, 0xFFFFFFFE))
	      return 0;
		// Do anti collision 2
		cmd[0] = 0x95;
		cmd[1] = 0x20;
		if (!sendData(cmd, 2, 0x00))
	      return 0;
		//Read 5 bytes. we will store at offset 2 for later use
		if (!readData(5, cmd+2))
	      return 0;
		// first 4 bytes belongs to last 4 UID bytes, we keep it.
		for (int i = 0; i < 4; i++) {
		  buffer[6 + i] = cmd[2+i];
		}
		//Enable RX CRC calculation
		if (!writeRegisterWithOrMask(CRC_RX_CONFIG, 0x01))
	      return 0;
		//Enable TX CRC calculation
		if (!writeRegisterWithOrMask(CRC_TX_CONFIG, 0x01))
	      return 0;
		//Send Select anti collision 2
		cmd[0] = 0x95;
		cmd[1] = 0x70;
		if (!sendData(cmd, 7, 0x00))
	      return 0;
		//Read 1 byte SAK into buffer[2]
		if (!readData(1, buffer + 2))
	      return 0;
		uidLength = 7;
	}
    return uidLength;
}

bool PN5180ISO14443::mifareBlockRead(uint8_t blockno, uint8_t *buffer) {
	bool success = false;
	uint16_t len;
	uint8_t cmd[2];
	// Send mifare command 30,blockno
	cmd[0] = 0x30;
	cmd[1] = blockno;
	if (!sendData(cmd, 2, 0x00))
	  return false;
	//Check if we have received any data from the tag
	delay(5);
	len = rxBytesReceived();
	if (len == 16) {
		// READ 16 bytes into  buffer
		if (readData(16, buffer))
		  success = true;
	}
	return success;
}


uint8_t PN5180ISO14443::mifareBlockWrite16(uint8_t blockno, uint8_t *buffer) {
	uint8_t cmd[2];
	// Clear RX CRC
	writeRegisterWithAndMask(CRC_RX_CONFIG, 0xFFFFFFFE);

	// Mifare write part 1
	cmd[0] = 0xA0;
	cmd[1] = blockno;
	sendData(cmd, 2, 0x00);
	readData(1, cmd);

	// Mifare write part 2
	sendData(buffer,16, 0x00);
	delay(10);

	// Read ACK/NAK
	readData(1, cmd);

	//Enable RX CRC calculation
	writeRegisterWithOrMask(CRC_RX_CONFIG, 0x1);
	return cmd[0];
}

bool PN5180ISO14443::mifareBlockWrite4(uint8_t pageno, const uint8_t *data) {
	// NTAG WRITE command (0xA2) — writes exactly 4 bytes to one page
	uint8_t cmd[6];
	cmd[0] = 0xA2;
	cmd[1] = pageno;
	cmd[2] = data[0];
	cmd[3] = data[1];
	cmd[4] = data[2];
	cmd[5] = data[3];

	// Clear RX CRC (ACK is only 4 bits, no CRC)
	writeRegisterWithAndMask(CRC_RX_CONFIG, 0xFFFFFFFE);

	if (!sendData(cmd, 6, 0x00)) {
		Serial.printf("PN5180: mifareBlockWrite4 page %d sendData failed\n", pageno);
		writeRegisterWithOrMask(CRC_RX_CONFIG, 0x1);
		return false;
	}

	// Poll RX_STATUS until ACK arrives or timeout (max ~15ms)
	// NTAG write time: typ 3.5ms, max 4.5ms at full field — may be slower at weak coupling
	uint32_t rxStatus;
	uint16_t rxLen = 0;
	for (int wait = 0; wait < 15; wait++) {
		delay(1);
		readRegister(RX_STATUS, &rxStatus);
		rxLen = rxStatus & RX_BYTES_RECEIVED_MASK;
		if (rxLen >= 1) break;
	}

	uint8_t ack = 0;
	if (rxLen >= 1) {
		readData(1, &ack);
	} else {
		Serial.printf("PN5180: mifareBlockWrite4 page %d no response (RX_STATUS=0x%08lX)\n",
		              pageno, (unsigned long)rxStatus);
	}

	// Re-enable RX CRC
	writeRegisterWithOrMask(CRC_RX_CONFIG, 0x1);

	// Return transceiver to Idle so the next sendData finds WaitTransmit state
	writeRegisterWithAndMask(SYSTEM_CONFIG, 0xfffffff8);

	// The ACK is a 4-bit response (0xA) delivered in the low nibble; the upper
	// nibble is undefined. Exact-matching the full byte misclassified real ACKs
	// (e.g. 0x2A) as NAKs and triggered destructive retries of successful writes.
	bool acked = (rxLen >= 1) && ((ack & 0x0F) == 0x0A);
	if (!acked) {
		Serial.printf("PN5180: mifareBlockWrite4 page %d NAK/bad ACK=0x%02X (rxLen=%d)\n", pageno, ack, rxLen);
	}

	return acked;
}

bool PN5180ISO14443::mifareHalt() {
	uint8_t cmd[2];
	//mifare Halt
	cmd[0] = 0x50;
	cmd[1] = 0x00;
	sendData(cmd, 2, 0x00);
	// HLTA never gets a response, so the transceiver parks in WaitReceive.
	// Abort back to Idle and clear IRQs — loadRFConfig during an active
	// transceive is illegal and wedges the state machine (sendData then fails
	// with state=3, and re-activation fails until the RF field is cycled).
	writeRegisterWithAndMask(SYSTEM_CONFIG, 0xfffffff8);
	clearIRQStatus(0xffffffff);
	return true;
}

bool PN5180ISO14443::mifareAuthenticate(uint8_t blockNo, uint8_t keyType, const uint8_t *key,
                                        const uint8_t *uid, uint8_t uidLen) {
	if (uidLen < 4) return false;
	// MIFARE Classic auth uses last 4 bytes of UID (matters for 7-byte UIDs)
	return mfcAuthenticate(key, keyType, blockNo, uid + (uidLen - 4));
}

uint8_t PN5180ISO14443::readCardSerial(uint8_t *buffer) {

    uint8_t response[10];
	uint8_t uidLength;
	// Always return 10 bytes
    // Offset 0..1 is ATQA
    // Offset 2 is SAK.
    // UID 4 bytes : offset 3 to 6 is UID, offset 7 to 9 to Zero
    // UID 7 bytes : offset 3 to 9 is UID
    for (int i = 0; i < 10; i++) response[i] = 0;
    uidLength = activateTypeA(response, 1);
	if ((response[0] == 0xFF) && (response[1] == 0xFF))
	  return 0;
	// check for valid uid
	if ((response[3] == 0x00) && (response[4] == 0x00) && (response[5] == 0x00) && (response[6] == 0x00))
	  return 0;
	if ((response[3] == 0xFF) && (response[4] == 0xFF) && (response[5] == 0xFF) && (response[6] == 0xFF))
	  return 0;
    for (int i = 0; i < 7; i++) buffer[i] = response[i+3];
	mifareHalt();
	return uidLength;
}

bool PN5180ISO14443::isCardPresent() {
    uint8_t buffer[10];
	return (readCardSerial(buffer) >=4);
}

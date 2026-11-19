/*
 * Copyright (c) Arduino s.r.l. and/or its affiliated companies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <Arduino.h>
#include "SocketHelpers.h"
#include <zephyr/net/ethernet.h>
#include "ZephyrClient.h"
#include "ZephyrServer.h"
#include "ClientTable.h"

#ifndef ETHERNET_SERVER_MAX_CLIENTS
#define ETHERNET_SERVER_MAX_CLIENTS 3
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(ethernet_phy) || DT_HAS_COMPAT_STATUS_OKAY(microchip_ksz8081)
using EthernetClient = ZephyrClient;

enum EthernetLinkStatus {
	Unknown,
	LinkON,
	LinkOFF
};

enum EthernetHardwareStatus {
	EthernetNoHardware,
	EthernetOk
};

class EthernetClass : public NetworkInterface {
public:
	EthernetClass() {
	}

	virtual ~EthernetClass() {
	}

	int begin(uint8_t *mac = nullptr, unsigned long timeout = 60000,
			  unsigned long responseTimeout = 4000);
	int maintain();
	EthernetLinkStatus linkStatus();
	EthernetHardwareStatus hardwareStatus();

	// Manual configuration
	int begin(uint8_t *mac, IPAddress ip);
	int begin(uint8_t *mac, IPAddress ip, IPAddress dns);
	int begin(uint8_t *mac, IPAddress ip, IPAddress dns, IPAddress gateway);
	int begin(uint8_t *mac, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnet,
			  unsigned long timeout = 60000, unsigned long responseTimeout = 4000);

	int begin(IPAddress ip) {
		return begin(nullptr, ip);
	}

	int begin(IPAddress ip, IPAddress dns) {
		return begin(nullptr, ip, dns);
	}

	int begin(IPAddress ip, IPAddress dns, IPAddress gateway) {
		return begin(nullptr, ip, dns, gateway);
	}

	int begin(IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnet) {
		return begin(nullptr, ip, dns, gateway, subnet);
	}

	/** Copies the interface MAC address into mac (6 bytes); the Arduino Ethernet library name. */
	void macAddress(uint8_t *mac) {
		MACAddress(mac);
	}

	void setRetransmissionTimeout(uint16_t milliseconds);
	void setRetransmissionCount(uint8_t num);
};

extern EthernetClass Ethernet;

/**
 * Arduino-style TCP server: available() returns a connected client that has data to read, so a
 * sketch polling it serves several connections; at most ETHERNET_SERVER_MAX_CLIENTS are kept, and a
 * connection beyond that is closed.
 */
class EthernetServer : public ZephyrServer {
public:
	explicit EthernetServer(uint16_t port) : ZephyrServer(port) {
	}

	EthernetClient available() {
		for (size_t i = 0; i < ETHERNET_SERVER_MAX_CLIENTS + 1; i++) {
			EthernetClient incoming = ZephyrServer::accept();
			if (!incoming) {
				break;
			}
			_clients.admit(incoming);
		}
		return _clients.pick();
	}

private:
	ClientTable<EthernetClient, ETHERNET_SERVER_MAX_CLIENTS> _clients;
};

#endif

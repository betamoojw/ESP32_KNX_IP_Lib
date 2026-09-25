#pragma once
#include "IPAddress.h"
struct WiFiStub { IPAddress localIP() { return IPAddress(192,168,1,10); } };
extern WiFiStub WiFi;

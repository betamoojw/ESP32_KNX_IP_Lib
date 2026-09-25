#pragma once
#include "Network.h"
struct EthernetStub : NetworkInterface { bool begin() { return true; } };
extern EthernetStub ETH;

#include "Client.h"
#include "../headers/SimConnect.h"


jetbridge::Client::Client(void* simconnect) {
  this->simconnect = simconnect;
  srand(time(0));

#ifdef _WIN32
  SimConnect_AddToClientDataDefinition(simconnect, kPacketDefinition, 0, sizeof(Packet));
  SimConnect_MapClientDataNameToID(simconnect, kPublicDownlinkChannel, kPublicDownlinkArea);
  SimConnect_MapClientDataNameToID(simconnect, kPublicUplinkChannel, kPublicUplinkArea);

  // We'll listen to downlink client data events.
  SimConnect_RequestClientData(simconnect, kPublicDownlinkArea, kDownlinkRequest, kPacketDefinition,
                               SIMCONNECT_CLIENT_DATA_PERIOD_ON_SET, SIMCONNECT_CLIENT_DATA_REQUEST_FLAG_CHANGED, 0);
#endif
}

void jetbridge::Client::request(const char data[]) {
  // Prepare the outgoing packet
  Packet* packet = new Packet(data);

#ifdef _WIN32
  // Transmit the request packet
  SimConnect_SetClientData(simconnect, kPublicUplinkArea, kPacketDefinition, 0, 0, sizeof(Packet), packet);
#endif
}


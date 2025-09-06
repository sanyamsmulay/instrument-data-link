#!/usr/bin/env python3

import socket
import struct
import time
import argparse
import random
import math
from dataclasses import dataclass
from typing import List, Tuple

# Constants from SimConnect.h
SIMCONNECT_RECV_ID_NULL = 0
SIMCONNECT_RECV_ID_EVENT = 4
SIMCONNECT_RECV_ID_SIMOBJECT_DATA = 8
SIMCONNECT_RECV_ID_CLIENT_DATA = 16
SIMCONNECT_RECV_ID_QUIT = 3

# Event IDs
EVENT_SIM_START = 0  # SIM_START from C++ code
EVENT_SIM_STOP = 208  # SIM_STOP from C++ code

# Request IDs
REQ_ID = 0  # Matches C++ REQ_ID
DEF_READ_ALL = 0  # Matches C++ DEF_READ_ALL

@dataclass
class SimConnectRecv:
    dwSize: int    # Total size of the structure
    dwVersion: int # SimConnect version number
    dwID: int      # ID of the structure type

    def pack(self) -> bytes:
        # Use 'I' for 32-bit unsigned int to match C++ DWORD (uint32_t)
        return struct.pack('QQQ', self.dwSize, self.dwVersion, self.dwID)

@dataclass
class SimConnectRecvEvent(SimConnectRecv):
    uGroupID: int  # Group ID
    uEventID: int  # Event ID
    dwData: int    # Additional event data

    def pack(self) -> bytes:
        base = super().pack()
        # Use 'I' for 32-bit unsigned int to match C++ DWORD (uint32_t)
        return base + struct.pack('QQQ', self.uGroupID, self.uEventID, self.dwData)

@dataclass
class SimConnectRecvSimObjectData(SimConnectRecv):
    dwRequestID: int    # Request ID
    dwObjectID: int     # Object ID (SIMCONNECT_OBJECT_ID_USER = 0 for user aircraft)
    dwDefineID: int     # Define ID (matches the definition used in SimConnect_AddToDataDefinition)
    dwFlags: int        # Flags
    dwentrynumber: int  # Entry number when multiple objects returned
    dwoutof: int       # Total number of objects being returned
    dwDefineCount: int # Number of Define IDs in the data
    dwData: List[float] # Variable length array of data values

    def pack(self) -> bytes:
        base = super().pack()
        # Use 'I' for 32-bit unsigned int to match C++ DWORD (uint32_t)
        header = struct.pack('<QQQQQQQ', 
                           self.dwRequestID,
                           self.dwObjectID,
                           self.dwDefineID,
                           self.dwFlags,
                           self.dwentrynumber,
                           self.dwoutof,
                           self.dwDefineCount)
        data = b''.join(struct.pack('d', d) for d in self.dwData)
        return base + header + data

class SimDataSender:
    def __init__(self, host: str = 'localhost', port: int = 52021):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.addr = (host, port)

    def print_packet(self, packet, packet_type: str) -> None:
        """Print packet details in a human-readable format"""
        
        print("\nPacket in Hex:")
        hex_str = " ".join(f"{b:02x}" for b in packet.pack())
        print(hex_str)

        print(f"\n=== Sending {packet_type} Packet ===")
        print("Base Structure:")
        print(f"  Size: {packet.dwSize} bytes")
        print(f"  Version: {packet.dwVersion}")
        type_map = {
            SIMCONNECT_RECV_ID_NULL: 'NULL',
            SIMCONNECT_RECV_ID_EVENT: 'EVENT',
            SIMCONNECT_RECV_ID_SIMOBJECT_DATA: 'SIMOBJECT_DATA',
            SIMCONNECT_RECV_ID_CLIENT_DATA: 'CLIENT_DATA',
            SIMCONNECT_RECV_ID_QUIT: 'QUIT'
        }
        print(f"  ID: {packet.dwID} (Type: {type_map.get(packet.dwID, 'UNKNOWN')})")
        
        if isinstance(packet, SimConnectRecvEvent):
            print("Event Data:")
            print(f"  Group ID: {packet.uGroupID}")
            print(f"  Event ID: {packet.uEventID}")
            print(f"  Data: {packet.dwData}")
        elif isinstance(packet, SimConnectRecvSimObjectData):
            print("SimObject Data:")
            print(f"  Request ID: {packet.dwRequestID}")
            print(f"  Object ID: {packet.dwObjectID}")
            print(f"  Define ID: {packet.dwDefineID}")
            print(f"  Flags: {packet.dwFlags}")
            print(f"  Entry: {packet.dwentrynumber} of {packet.dwoutof}")
            print(f"  Define Count: {packet.dwDefineCount}")
            print(f"  Data Values: {packet.dwData}")
        print("==========================\n")
        
        if isinstance(packet, SimConnectRecvEvent):
            print(f"Event ID: {packet.uEventID}")
            print(f"Data: {packet.dwData}")
        elif isinstance(packet, SimConnectRecvSimObjectData):
            print(f"SimObject Data:")
            print(f"  Request ID: {packet.dwRequestID}")
            print(f"  Object ID: {packet.dwObjectID}")
            print(f"  Define ID: {packet.dwDefineID}")
            print(f"  Flags: {packet.dwFlags}")
            print(f"  Entry: {packet.dwentrynumber} of {packet.dwoutof}")
            print(f"  Define Count: {packet.dwDefineCount}")
            print(f"  Data Values: {packet.dwData}")
        print("==========================\n")

    def send_event(self, event_type: int, event_id: int = 0, data: int = 0) -> None:
        """Send a simple event packet"""
        packet = SimConnectRecvEvent(
            dwSize=8*6,      # Should be the calculated size of the structure
            dwVersion=0,    # Version is usually 0
            dwID=event_type,
            uGroupID=0,     # Default group
            uEventID=event_id,
            dwData=data
        )
        self.print_packet(packet, 'Event')
        self.sock.sendto(packet.pack(), self.addr)

    def send_simobject_data(self, data: List[float]) -> None:
        """Send simulated aircraft data"""
        print("\nPreparing SimObject Data:")
        print(f"  Altitude: {data[0]:.1f} ft")
        print(f"  Heading: {data[1]:.1f}°")
        print(f"  Airspeed: {data[2]:.1f} knots")
        print(f"  Vertical Speed: {data[3]:.1f} ft/min")
        
        base_size = 8*6  # Size of SimConnectRecv (12) + SimObjectData header (28)
        data_size = len(data) * 8  # Each float is 8 bytes
        packet = SimConnectRecvSimObjectData(
            dwSize=base_size + data_size,
            dwVersion=0,    # Version is usually 0
            dwID=SIMCONNECT_RECV_ID_SIMOBJECT_DATA,
            dwRequestID=REQ_ID,
            dwObjectID=0,   # SIMCONNECT_OBJECT_ID_USER
            dwDefineID=DEF_READ_ALL,   # DEF_READ_ALL
            dwFlags=0,
            dwentrynumber=1,
            dwoutof=1,
            dwDefineCount=len(data),
            dwData=data
        )
        self.print_packet(packet, "SimObject Data")
        self.sock.sendto(packet.pack(), self.addr)

    def run_test_sequence(self, interval: float = 10.0) -> None:
        """Run a test sequence of data packets"""
        print(f"Sending test data to {self.addr[0]}:{self.addr[1]}")
        try:
            # Send initial event to simulate connection
            self.send_event(SIMCONNECT_RECV_ID_EVENT, EVENT_SIM_START)
            print("Sent SIM_START event")

            altitude = 1000.0
            heading = 180.0
            airspeed = 120.0
            vertical_speed = 0.0

            # while True:
            #     # Simulate some simple flight dynamics
            #     altitude += vertical_speed * interval / 60  # Convert from feet/min to feet/interval
            #     heading = (heading + 1) % 360  # Slowly turn
            #     airspeed += (random.random() - 0.5) * 2  # Random speed variations
            #     vertical_speed = math.sin(time.time() / 10) * 500  # Oscillating vertical speed

            #     # Send simulated aircraft data
            #     data = [
            #         altitude,        # Altitude (feet)
            #         heading,         # Heading (degrees)
            #         airspeed,        # Airspeed (knots)
            #         vertical_speed,  # Vertical speed (feet/min)
            #         # Add more simulation variables as needed
            #     ]
            #     self.send_simobject_data(data)
            time.sleep(interval)
            self.send_event(SIMCONNECT_RECV_ID_EVENT, EVENT_SIM_STOP)
            print("Sent SIM_STOP event")
            self.send_event(SIMCONNECT_RECV_ID_QUIT)
            print("Sent QUIT event")
        except KeyboardInterrupt:
            print("\nStopping data transmission")
            # Send stop event
            self.send_event(EVENT_SIM_STOP)
            print("Sent SIM_STOP event")
            # Send quit event before exiting
            self.send_event(SIMCONNECT_RECV_ID_QUIT)
            print("Sent QUIT event")

def main():
    parser = argparse.ArgumentParser(description='Send simulated flight data')
    parser.add_argument('--host', default='localhost',
                      help='Target host (default: localhost)')
    parser.add_argument('--port', type=int, default=52021,
                      help='Target port (default: 52021)')
    parser.add_argument('--interval', type=float, default=1.0,
                      help='Data send interval in seconds (default: 0.1)')
    
    args = parser.parse_args()
    
    sender = SimDataSender(args.host, args.port)
    sender.run_test_sequence(args.interval)

if __name__ == '__main__':
    main()

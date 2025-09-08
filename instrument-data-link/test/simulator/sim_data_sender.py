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
    dwData: bytes      # Binary data matching the SimVars structure

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
        return base + header + self.dwData

class SimDataSender:
    def __init__(self, host: str = 'localhost', port: int = 52022):
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
            dwSize=8*10,      # Should be the calculated size of the structure
            dwVersion=0,    # Version is usually 0
            dwID=event_type,
            uGroupID=0,     # Default group
            uEventID=event_id,
            dwData=data
        )
        self.print_packet(packet, 'Event')
        self.sock.sendto(packet.pack(), self.addr)

    def send_simobject_data(self, data: bytes) -> None:
        """Send simulated aircraft data
        
        Args:
            data: Binary data matching the SimVars structure
        """
        # Extract some key values for display
        # Note: Each double is 8 bytes, each string32 is 32 bytes
        # Count carefully through simvarDefs.h to get correct offsets
        alt_offset = 76  # altAltitude
        spd_offset = 77  # asiAirspeed
        hdg_offset = 79  # hiHeading
        vs_offset = 80   # vsiVerticalSpeed
        
        altitude = struct.unpack('d', data[alt_offset*8:(alt_offset+1)*8])[0]
        airspeed = struct.unpack('d', data[spd_offset*8:(spd_offset+1)*8])[0]
        heading = struct.unpack('d', data[hdg_offset*8:(hdg_offset+1)*8])[0]
        vertical_speed = struct.unpack('d', data[vs_offset*8:(vs_offset+1)*8])[0]
        
        print("\nPreparing SimObject Data:")
        print(f"  Altitude: {altitude:.1f} ft")
        print(f"  Heading: {heading:.1f}°")
        print(f"  Airspeed: {airspeed:.1f} knots")
        print(f"  Vertical Speed: {vertical_speed:.1f} ft/min")
        
        base_size = 8*10  # 
        data_size = len(data)  # Size of the binary data
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
            dwDefineCount=data_size // 8,  # Number of doubles
            dwData=data
        )
        self.print_packet(packet, "SimObject Data")
        self.sock.sendto(packet.pack(), self.addr)

    def construct_data(self, altitude: float, heading: float, airspeed: float, vertical_speed: float, bank_angle: float) -> bytes:
        """Construct the data array matching the SimVars structure in C++.
        The order must match the variable declarations in simvarDefs.h.
        String32 variables are allocated 32 bytes each.
        
        Args:
            altitude: Current altitude in feet
            heading: Current heading in degrees
            airspeed: Current airspeed in knots
            vertical_speed: Current vertical speed in feet/min
            bank_angle: Current bank angle in degrees
            
        Returns:
            bytes: Binary data matching the SimVars structure
        """
        # Create a bytearray to store the binary data
        data = bytearray()
        
        # Helper function to add a double to the bytearray
        def add_double(value: float):
            data.extend(struct.pack('d', value))
            
        # Helper function to add a string32 to the bytearray
        def add_string32(value: str):
            # Convert string to bytes, pad with nulls to 32 bytes
            encoded = value.encode('utf-8')[:32]
            encoded = encoded.ljust(32, b'\0')
            data.extend(encoded)
        
        # commented to only send simConnect variables
        # Add connected state
        # add_double(1.0)    # connected
        # print("added connected, size: ", len(data))
        # # Jetbridge vars
        # for _ in range(16):  # 16 jetbridge variables
        #     add_double(0.0)
        # print("added jetbridge, size: ", len(data))
        
        # SwitchBox vars
        # for _ in range(13):  # 4 encoders + 7 buttons + mode + park brake
        #     add_double(0.0)
        # print("added switchbox, size: ", len(data))
        
        # Aircraft identification
        add_string32('')    # Title (string32)
        add_double(120.0)   # cruiseSpeed
        add_double(23.7)    # dcVolts
        add_double(0.0)     # batteryLoad
        print("added aircraft identification, size: ", len(data))
        
        # Power/Lights panel
        add_double(0.0)     # lightStates
        add_double(1.0)     # tfFlapsCount
        add_double(0.0)     # tfFlapsIndex
        add_double(1.0)     # parkingBrakeOn
        add_double(3.0)     # pushbackState
        add_double(0.0)     # apuStartSwitch
        add_double(0.0)     # apuPercentRpm
        print("added power/lights panel, size: ", len(data))
        
        # Radio panel
        add_double(0.0)     # com1Status
        add_double(1.0)     # com1Transmit
        add_double(119.225) # com1Freq
        add_double(124.850) # com1Standby
        add_double(110.50)  # nav1Freq
        add_double(113.90)  # nav1Standby
        add_double(0.0)     # com2Status
        add_double(0.0)     # com2Transmit
        add_double(124.850) # com2Freq
        add_double(124.850) # com2Standby
        add_double(110.50)  # nav2Freq
        add_double(113.90)  # nav2Standby
        add_double(1.0)     # com1Receive
        add_double(0.0)     # com2Receive
        add_double(394.0)   # adfFreq
        add_double(368.0)   # adfStandby
        add_double(0.0)     # com1Volume
        add_double(0.0)     # com2Volume
        add_double(0.0)     # seatBeltsSwitch
        add_double(0.0)     # transponderState
        add_double(4608.0)  # transponderCode
        print("added radio panel, size: ", len(data))

        # Autopilot panel
        add_double(altitude)        # altAltitude
        add_double(airspeed)        # asiAirspeed
        add_double(0.0)             # asiMachSpeed
        add_double(heading)         # hiHeading
        add_double(vertical_speed)  # vsiVerticalSpeed
        add_double(1.0)             # autopilotAvailable
        add_double(0.0)             # autopilotEngaged
        add_double(0.0)             # flightDirectorActive
        add_double(heading)         # autopilotHeading
        add_double(0.0)             # autopilotHeadingLock
        add_double(1.0)             # autopilotHeadingSlotIndex
        add_double(0.0)             # autopilotLevel
        add_double(altitude)        # autopilotAltitude
        add_double(altitude)        # autopilotAltitude3
        add_double(0.0)             # autopilotAltLock
        add_double(0.0)             # autopilotNav1Lock
        add_double(0.0)             # gpsDrivesNav1
        add_double(0.0)             # autopilotPitchHold
        add_double(vertical_speed)  # autopilotVerticalSpeed
        add_double(0.0)             # autopilotVerticalHold
        add_double(1.0)             # autopilotVsSlotIndex
        add_double(airspeed)        # autopilotAirspeed
        add_double(0.0)             # autopilotMach
        add_double(0.0)             # autopilotAirspeedHold
        add_double(0.0)             # autopilotApproachHold
        add_double(0.0)             # autopilotGlideslopeHold
        add_double(0.0)             # throttlePosition
        add_double(0.0)             # autothrottleActive
        print("added autopilot panel, size: ", len(data))
        
        # Additional instruments
        add_double(29.92)           # altKollsman
        add_double(0.0)             # adiPitch
        add_double(bank_angle)      # adiBank
        add_double(airspeed)        # asiTrueSpeed
        add_double(-14.0)           # asiAirspeedCal
        add_double(heading)         # hiHeadingTrue
        add_double(altitude)        # altAboveGround
        add_double(0.0)             # tcRate
        add_double(0.0)             # tcBall
        add_double(0.0)             # tfElevatorTrim
        add_double(0.0)             # tfRudderTrim
        add_double(0.0)             # tfSpoilersPosition
        add_double(0.0)             # tfAutoBrake
        add_double(43200.0)         # dcUtcSeconds
        add_double(46800.0)         # dcLocalSeconds
        add_double(0.0)             # dcFlightSeconds
        add_double(26.2)            # dcTempC
        add_double(1.0)             # numberOfEngines
        add_double(0.0)             # rpmEngine
        add_double(0.0)             # rpmPercent
        add_double(0.0)             # rpmElapsedTime
        add_double(50.0)            # fuelCapacity
        add_double(0.0)             # fuelQuantity
        add_double(0.0)             # fuelLeftPercent
        add_double(0.0)             # fuelRightPercent
        add_double(0.0)             # vor1Obs
        add_double(0.0)             # vor1RadialError
        add_double(0.0)             # vor1GlideSlopeError
        add_double(0.0)             # vor1ToFrom
        add_double(0.0)             # vor1GlideSlopeFlag
        add_double(0.0)             # vor2Obs
        add_double(0.0)             # vor2RadialError
        add_double(0.0)             # vor2ToFrom
        add_double(0.0)             # navHasLocalizer
        add_double(0.0)             # navLocalizer
        add_double(0.0)             # gpsWpCrossTrk
        add_double(0.0)             # adfRadial
        add_double(0.0)             # adfCard
        add_double(1.0)             # gearRetractable
        add_double(100.0)           # gearLeftPos
        add_double(100.0)           # gearCentrePos
        add_double(100.0)           # gearRightPos
        add_double(0.0)             # rudderPosition
        add_double(0.0)             # brakeLeftPedal
        add_double(0.0)             # brakeRightPedal
        add_double(0.0)             # oilTemp1
        add_double(0.0)             # oilTemp2
        add_double(0.0)             # oilTemp3
        add_double(0.0)             # oilTemp4
        add_double(0.0)             # oilPressure1
        add_double(0.0)             # oilPressure2
        add_double(0.0)             # oilPressure3
        add_double(0.0)             # oilPressure4
        add_double(0.0)             # exhaustGasTemp1
        add_double(0.0)             # exhaustGasTemp2
        add_double(0.0)             # exhaustGasTemp3
        add_double(0.0)             # exhaustGasTemp4
        add_double(0.0)             # engineType
        add_double(0.0)             # engineMaxRpm
        add_double(0.0)             # turbineEngine1N1
        add_double(0.0)             # turbineEngine2N1
        add_double(0.0)             # turbineEngine3N1
        add_double(0.0)             # turbineEngine4N1
        add_double(0.0)             # propRpm
        add_double(0.0)             # engineManifoldPressure
        add_double(0.0)             # engineFuelFlow1
        add_double(0.0)             # engineFuelFlow2
        add_double(0.0)             # engineFuelFlow3
        add_double(0.0)             # engineFuelFlow4
        add_double(1.0)             # suctionPressure
        add_double(0.0)             # onGround
        add_double(0.0)             # gForce
        print("added additional instruments, size: ", len(data))
        
        # Add remaining string32 variables
        add_string32('')            # atcTailNumber
        add_string32('')            # atcCallSign
        add_string32('')            # atcFlightNumber
        add_double(0.0)             # atcHeavy
        print("added remaining string32 variables, size: ", len(data))
        
        # Add remaining doubles
        # internal variables - not including for now
        # add_double(-999.0)          # landingRate
        # add_double(0.0)             # skytrackState
        print("added remaining doubles, size: ", len(data))

        print("Data size: ", len(data))
        return bytes(data)

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

            while True:
                print("Sim step")
                # Simulate some simple flight dynamics
                altitude += vertical_speed * interval / 60  # Convert from feet/min to feet/interval
                heading = (heading + 1) % 360  # Slowly turn
                airspeed += (random.random() - 0.5) * 2  # Random speed variations
                vertical_speed = math.sin(time.time() / 10) * 500  # Oscillating vertical speed
                bank_angle = math.sin(time.time() / 10) * 10  # Oscillating bank angle
                
                # Construct data array matching the SimVars structure
                data = self.construct_data(
                    altitude=altitude,
                    heading=heading,
                    airspeed=airspeed,
                    vertical_speed=vertical_speed,
                    bank_angle=bank_angle
                )
                self.send_simobject_data(data)
                time.sleep(interval)
                
        except KeyboardInterrupt:
            print("\nStopping data transmission")
            # Send stop event
            self.send_event(SIMCONNECT_RECV_ID_EVENT, EVENT_SIM_STOP)
            print("Sent SIM_STOP event")
            # Send quit event before exiting
            self.send_event(SIMCONNECT_RECV_ID_QUIT)
            print("Sent QUIT event")

def main():
    parser = argparse.ArgumentParser(description='Send simulated flight data')
    parser.add_argument('--host', default='localhost',
                      help='Target host (default: localhost)')
    parser.add_argument('--port', type=int, default=52022,
                      help='Target port (default: 52022)')
    parser.add_argument('--interval', type=float, default=1.0,
                      help='Data send interval in seconds (default: 0.1)')
    
    args = parser.parse_args()
    
    sender = SimDataSender(args.host, args.port)
    sender.run_test_sequence(args.interval)

if __name__ == '__main__':
    main()

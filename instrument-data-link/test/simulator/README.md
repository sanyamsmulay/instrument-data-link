# Simulator Test Tools

This directory contains tools for testing the instrument-data-link without a real simulator.

## sim_data_sender.py

A Python script that simulates a flight simulator by sending UDP packets in SIMCONNECT_RECV format.

### Usage

1. Make the script executable:
```bash
chmod +x sim_data_sender.py
```

2. Run with default settings (localhost:52021):
```bash
./sim_data_sender.py
```

3. Run with custom settings:
```bash
./sim_data_sender.py --host localhost --port 52021 --interval 0.05
```

### Data Format

The script sends two types of packets:

1. SimConnect Events (SIMCONNECT_RECV_EVENT)
   - 16 bytes total
   - Contains: dwID, dwSize, dwData, uEventID

2. SimObject Data (SIMCONNECT_RECV_SIMOBJECT_DATA)
   - Variable size based on data array
   - Contains simulated flight data:
     - Altitude (feet)
     - Heading (degrees)
     - Airspeed (knots)
     - Vertical speed (feet/min)

### Testing

1. Start the instrument-data-link program
2. Run this script to send simulated data
3. The instrument-data-link should connect and start receiving data
4. Press Ctrl+C to stop the script (it will send a QUIT event)

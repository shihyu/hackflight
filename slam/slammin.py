#!/usr/bin/env python3

'''
slammin.py : Runs SLAM from sensor telemetry retrieved over comm port

Copyright (C) Matt Lubas & Simon D. Levy 2016

This code is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as 
published by the Free Software Foundation, either version 3 of the 
License, or (at your option) any later version.
This code is distributed in the hope that it will be useful,     
but WITHOUT ANY WARRANTY without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU Lesser General Public License 
along with this code.  If not, see <http:#www.gnu.org/licenses/>.
'''

from msppg import MSP_Parser as Parser, serialize_SONARS_Request, serialize_ATTITUDE_Request
import serial

from sys import argv

if len(argv) < 3:

    print('Usage: python3 %s PORT BAUD' % argv[0])
    print('Example: python3 %s /dev/ttyUSB0 57600' % argv[0])
    exit(1)

port = serial.Serial(argv[1], int(argv[2]))

parser = Parser()

sonars_request = serialize_SONARS_Request()
attitude_request = serialize_ATTITUDE_Request()

def sonars_handler(forward, back, left, right):
    print('Sonars: forward: %d   back: %d  left: %d  right: %d' % (forward, back, left, right))
    port.write(sonars_request)

parser.set_SONARS_Handler(sonars_handler)

port.write(sonars_request)

while True:

    try:

        parser.parse(port.read(1))

    except KeyboardInterrupt:

        break

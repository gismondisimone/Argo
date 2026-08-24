import time
import serial # type: ignore

uart = serial.Serial("/dev/serial0", baudrate=115200, timeout=1)

uart.write(b'AT+SETCFG=0,1,1,1\r\n') # board id(0-7), board mode(0tag-1base), channel(0-1), data rate(0(7.75 GHz to 8.25 GHz)-1(6.25 GHz to 6.75 GHz(best)))

# antenna delay setup : uart.write('AT+SETDEV=10,16336,1,0.018,0.642,1.0000,0.00,0,0\r\n') (if values are constantly too high or too low)

time.sleep(1)

if uart.in_waiting:
    message = uart.read(uart.in_waiting)
    print(message)
    
time.sleep(3)    

uart.write(b'AT+SAVE\r\n')

time.sleep(3)

if uart.in_waiting:
    message = uart.read(uart.in_waiting)
    print(message)
    
    
uart.write(b'AT+GETCFG\r\n')

time.sleep(0.1)

if uart.in_waiting:
    message = uart.read(uart.in_waiting)
    print(message)

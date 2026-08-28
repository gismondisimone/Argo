from machine import UART, Pin # type: ignore
import time

# Initialize UART 0 on Pico, TX pin is GP0 and RX pin is GP1
uart = UART(0, baudrate=115200, tx=Pin(0), rx=Pin(1))

uart.write(b'AT+SETCFG=0,1,1,1\r\n') # board id(0-7), board mode(0-tag, 1-base), channel(0-1), data rate(0 or 1)

time.sleep(1)

if uart.any():
    message = uart.read()
    print(message)
    
time.sleep(3)    

uart.write('AT+SAVE\r\n')

time.sleep(3)

if uart.any():
    message = uart.read()
    print(message)
    
    
uart.write('AT+GETCFG\r\n')

time.sleep(0.1)

if uart.any():
    message = uart.read()
    print(message)
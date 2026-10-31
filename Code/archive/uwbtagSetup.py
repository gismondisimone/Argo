from machine import UART, Pin # type: ignore
import time

# Initialize UART0 (or UART1 if needed)
uart = UART(0, baudrate=115200, tx=Pin(0), rx=Pin(1), timeout=1000)

# Send command to set configuration
uart.write(b'AT+SETCFG=0,0,1,1\r\n')  # board id(0-7), board mode(0-tag, 1-base), channel(0-1), data rate(0 or 1)

time.sleep(1)

# Check for and read response
if uart.any():
    message = uart.read()
    print("Response to AT+SETCFG:", message)

time.sleep(3)

# Save configuration
uart.write(b'AT+SAVE\r\n')

time.sleep(3)

# Check for and read response
if uart.any():
    message = uart.read()
    print("Response to AT+SAVE:", message)

# Get configuration
uart.write(b'AT+GETCFG\r\n')

time.sleep(0.1)

# Check for and read response
if uart.any():
    message = uart.read()
    print("Response to AT+GETCFG:", message)

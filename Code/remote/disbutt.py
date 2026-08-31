from machine import UART, Pin, SPI #type:ignore
import time
import st7789 # Ensure st7789.py driver is uploaded to MicroPython root #type:ignore

# UART Setup (BU03 UWB Kit)
uart = UART(0, baudrate=115200, tx=Pin(0), rx=Pin(1), timeout=1000)

# ST7789 Display Setup
spi = SPI(0, baudrate=30000000, sck=Pin(18), mosi=Pin(19))

display = st7789.ST7789(
    spi,
    240,
    280,
    reset=Pin(15, Pin.OUT),
    dc=Pin(14, Pin.OUT),
    cs=Pin(17, Pin.OUT),
    backlight=Pin(13, Pin.OUT),
    rotation=0
)
display.init()
display.fill(st7789.BLACK)

# 3. Button Setup (Pull-up)
btn_up = Pin(2, Pin.IN, Pin.PULL_UP)
btn_down = Pin(3, Pin.IN, Pin.PULL_UP)
btn_select = Pin(4, Pin.IN, Pin.PULL_UP)
btn_back = Pin(5, Pin.IN, Pin.PULL_UP)

def send_at_command(cmd, label):
    uart.write(cmd)
    time.sleep(0.5)
    response = b""
    if uart.any():
        response = uart.read()
    
    text_res = response.decode('utf-8', 'ignore').strip()
    print(f"{label}: {text_res}")
    
    # Update LCD Screen
    display.fill(st7789.BLACK)
    display.text(label, 10, 10, st7789.CYAN)
    display.text(text_res[:20], 10, 40, st7789.WHITE)
    return text_res

# Initial Configuration Sequence
send_at_command(b'AT+SETCFG=0,0,1,1\r\n', "Set CFG")
time.sleep(1)
send_at_command(b'AT+SAVE\r\n', "Save CFG")
time.sleep(1)
send_at_command(b'AT+GETCFG\r\n', "Get CFG")

# Main Loop: Read Buttons & Update Display
while True:
    if btn_up.value() == 0:
        display.fill(st7789.BLACK)
        display.text("Button: UP", 10, 10, st7789.GREEN)
        time.sleep(0.2) # Basic debouncing

    if btn_down.value() == 0:
        display.fill(st7789.BLACK)
        display.text("Button: DOWN", 10, 10, st7789.GREEN)
        time.sleep(0.2)

    if btn_select.value() == 0:
        send_at_command(b'AT+GETCFG\r\n', "Query CFG")
        time.sleep(0.2)

    if btn_back.value() == 0:
        display.fill(st7789.BLACK)
        display.text("Button: BACK", 10, 10, st7789.RED)
        time.sleep(0.2)

    time.sleep(0.05)
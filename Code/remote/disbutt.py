from machine import UART, Pin, SPI #type:ignore
import time
import st7789 #type:ignore

# Setup
uart = UART(0, baudrate=115200, tx=Pin(0), rx=Pin(1), timeout=1000)

spi = SPI(0, baudrate=30000000, sck=Pin(18), mosi=Pin(19))
display = st7789.ST7789(
    spi, 240, 280,
    reset=Pin(15, Pin.OUT), dc=Pin(14, Pin.OUT), cs=Pin(17, Pin.OUT),
    backlight=Pin(13, Pin.OUT), rotation=0
)
display.init()
display.fill(st7789.BLACK)

btn_home = Pin(2, Pin.IN, Pin.PULL_UP)
btn_walk = Pin(3, Pin.IN, Pin.PULL_UP)
btn_stop = Pin(4, Pin.IN, Pin.PULL_UP)
btn_coord = Pin(5, Pin.IN, Pin.PULL_UP)

def send_uwb_signal(button_name):
    """
    Sends a custom data payload to Board 0 over UWB.
    Syntax: AT+DATA=<target_id>,<data> or AT+SEND=<length>,<data>
    (Adjust exact AT payload format per BU03 firmware spec)
    """
    cmd = f"AT+DATA=0,BTN_{button_name}\r\n".encode('utf-8')
    uart.write(cmd)
    
    # Update local screen
    display.fill(st7789.BLACK)
    display.text("Sent Signal:", 10, 10, st7789.CYAN)
    display.text(f"BTN_{button_name}", 10, 40, st7789.GREEN)
    print(f"Transmitted: BTN_{button_name}")

# Main Event Loop
while True:
    if btn_home.value() == 0:
        send_uwb_signal("HOME")
        time.sleep(0.25) # Debounce delay

    if btn_walk.value() == 0:
        send_uwb_signal("WALK")
        time.sleep(0.25)

    if btn_stop.value() == 0:
        send_uwb_signal("STOP")
        time.sleep(0.25)

    if btn_coord.value() == 0:
        send_uwb_signal("COORD")
        time.sleep(0.25)

    time.sleep(0.05)

# another day another making the 5 minutes to not lose my streak
# today i got a hairdresser appointment so im probably not gonna work much, but imma try in the afternoon.
# tbf i don't want to waste time like this but i don't have much to do until we get the components, if i had something i'd do it.
# but hackatime doesn't care abt ibispaint time ig, so this is the only way to make my 5 minutes :(
# iv'e been working on the mascots and the merch art, we only need to get the banners text to write it and then theyre all good.
# we need to get the measurements right this time tho
# like julio said, it's been years that my school has partecipied to this competition and we still get the measurements wrong for the banners.

# MADOOOOOOOOOOOOOOOO è tardissimo mi sono completamente dimenticata della streak stavo per andare a dormire e dimenticarmene xd
# menomale che ho ancora un paio di braincells e scrivendo a simo mi sono ricordata :D
# cmq non ho fatto un cazzo oggi, domani biblio che poi andiamo a casa di simo e saldo la santissima millefori e cerco di capire come collegare le robe
# poi sabato (?) arriva lo schermo, da la easy ma devo capire come mettere in display le robe e come accocchiare tutto
# di conseguenza devo pure cominciare a fare la parte dell'esp, sarebbe un'ottima idea di partenza per domani tbf

# domani prima task ESP code
# btw hackatime funziona come al cazzo ommiodio ogni 2 minuti si updatea ommiodio
# OHHH VELOCE CHE DEVO GIOCARE DIO CANE
# credo che un bro stia cercando di rizzarmi HELP
# è un pò patato però

# devo pure finire di trasferire la roba su sto pc xd
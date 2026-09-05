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

# yo allora welcome back a un'altra sessione di non ho nulla da fare quindi mi prendo la streak del giorno
# sono attualmente le due di notte, fa lowkey freddo e mia madre (o gioia) russa
# per qualche motivo lo sticker di oggi non si può vedere??? ma non me ne frega cause so che sarà fire comunque
# tochi è un patato
# nyzro mmmmmmmm non tanto
# ce non è che non mi piace giocare con lui è solo che ogni tanto è propio giocatore tossico da spam reportare
# tornando a noi
# oggi ho fatto un bel po di roba, che dovrà decisamente essere polishata meglio perché gia non mi ricordo nulla
# domani arrivano le robe tra cui lo schermo per il telecomando, devo capire come fare le saldature varie che poi simo deve fare la pcb
# sperando
# intanto stavo cominciando a sistemare i codici delle altre schede che dovranno cambiare quasi totalmente
# il porca madonna di attuatore lineare non serviva :sob: bastava benissimo anche solo un cazzo di stepper, sarebbe stato piu veloce piu preciso MA PURE UN DIOINCARROZZA DI SERVO
# io bho sti ingegneri
# devono tutti esplodere
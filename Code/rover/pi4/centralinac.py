import time
# Note: Low-level Decawave driver bindings (e.g., PyDW3000 or custom C daemon)
# are required to initialize the DW3000 registers over SPI.

BUTTON_MAP = {
    0x01: "HOME",
    0x02: "WALK",
    0x03: "STOP",
    0x04: "COORD",
    0x05: "EXTRA_1",
    0x06: "EXTRA_2"
}

def parse_uwb_frame(frame_bytes):
    # MAC Header length is 10 bytes:
    # [0..1] Frame Control, [2] Seq Num, [3..4] PAN ID (0xCADE), 
    # [5..6] Dest (0xFFFF), [7..8] Src (0x0001), [9] Frame Type
    if len(frame_bytes) < 11:
        return

    pan_id = (frame_bytes[4] << 8) | frame_bytes[3]
    frame_type = frame_bytes[9]

    if pan_id == 0xCADE and frame_type == 0xA1:
        button_code = frame_bytes[10]
        command_name = BUTTON_MAP.get(button_code, "UNKNOWN")
        
        print(f"[+] Received Command: {command_name} (Code: 0x{button_code:02X})")
        execute_rover_action(command_name)

def execute_rover_action(cmd):
    if cmd == "HOME":
        print("--> Rover navigating to HOME point...")
    elif cmd == "WALK":
        print("--> Rover starting WALK mode...")
    elif cmd == "STOP":
        print("--> Emergency STOP triggered!")
    elif cmd == "COORD":
        print("--> Fetching current coordinates...")

def send_display_update_to_remote(data_bytes):
    """
    Sends a 0xA2 telemetry frame back to the remote to update its TFT display.
    data_bytes: list or bytes object (up to 32 bytes)
    """
    header = [
        0x41, 0x88, 0x00,          # Frame Control + Dummy Seq Num
        0xDE, 0xCA,                # PAN ID: 0xCADE (Little Endian)
        0x01, 0x00,                # Destination: Remote (0x0001)
        0x02, 0x00,                # Source: Rover (0x0002)
        0xA2                       # Frame Type: DISPLAY
    ]
    payload = header + list(data_bytes[:32])
    # dw3000_transmit(payload)
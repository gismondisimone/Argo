import time
from gpiozero import DigitalOutputDevice, PWMOutputDevice  # type: ignore

# BCM GPIO pin numbers: set these to match the motor driver wiring.
LEFT_PWM_PIN = 27
LEFT_DIR_PIN = 22
RIGHT_PWM_PIN = 23
RIGHT_DIR_PIN = 24

# Motor power as a fraction from 0.0 to 1.0.
DRIVE_SPEED = 0.20
TURN_SPEED = 0.20

# Set this experimentally so one turn lasts about 45 degrees.
TURN_45_SECONDS = 1.0
STRAIGHT_SECONDS = 1.0
PAUSE_SECONDS = 1.0


def set_motors(left_speed, right_speed, left_motor, right_motor):
    left_motor[0].value = abs(left_speed)
    right_motor[0].value = abs(right_speed)

    # The ZS-Z11H DIR input is active low for reverse.
    left_motor[1].value = left_speed >= 0
    right_motor[1].value = right_speed >= 0


def stop_motors(left_motor, right_motor):
    left_motor[0].off()
    right_motor[0].off()


def run_step(label, left_speed, right_speed, duration, left_motor, right_motor):
    print(label)
    set_motors(left_speed, right_speed, left_motor, right_motor)
    time.sleep(duration)
    stop_motors(left_motor, right_motor)
    time.sleep(PAUSE_SECONDS)


def main():
    pins = (LEFT_PWM_PIN, LEFT_DIR_PIN, RIGHT_PWM_PIN, RIGHT_DIR_PIN)
    if any(pin is None for pin in pins):
        raise ValueError("Set all four motor GPIO pins at the top of this script.")

    left_motor = (
        PWMOutputDevice(LEFT_PWM_PIN, frequency=1000, initial_value=0),
        DigitalOutputDevice(LEFT_DIR_PIN, initial_value=True),
    )
    right_motor = (
        PWMOutputDevice(RIGHT_PWM_PIN, frequency=1000, initial_value=0),
        DigitalOutputDevice(RIGHT_DIR_PIN, initial_value=True),
    )

    try:
        print("Motor test starting. Press Ctrl+C to stop.")

        # Straight line: both wheels forward, then both in reverse.
        run_step("Forward", DRIVE_SPEED, -DRIVE_SPEED, STRAIGHT_SECONDS,
                 left_motor, right_motor)
        run_step("Backward", -DRIVE_SPEED, DRIVE_SPEED, STRAIGHT_SECONDS,
                 left_motor, right_motor)

        # Pivot turns: the wheels move in opposite directions.
        run_step("Pivot left (about 45 degrees)", -TURN_SPEED, -TURN_SPEED,
                 TURN_45_SECONDS, left_motor, right_motor)
        run_step("Pivot right (about 45 degrees)", TURN_SPEED, TURN_SPEED,
                 TURN_45_SECONDS, left_motor, right_motor)

        # One-wheel turns: stop one wheel and drive the other.
        run_step("Left turn: right wheel only", 0, -TURN_SPEED,
                 TURN_45_SECONDS, left_motor, right_motor)
        run_step("Right turn: left wheel only", TURN_SPEED, 0,
                 TURN_45_SECONDS, left_motor, right_motor)

        print("Motor test complete.")
    except KeyboardInterrupt:
        print("Motor test interrupted.")
    finally:
        stop_motors(left_motor, right_motor)
        for device in (*left_motor, *right_motor):
            device.close()


if __name__ == "__main__":
    main()

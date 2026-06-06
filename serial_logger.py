import serial
import serial.tools.list_ports
import sys
import time

BAUD = 115200

def find_port():
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        if "USB" in p.description or "CP210" in p.description or "CH340" in p.description:
            return p.device
    if ports:
        return ports[0].device
    return None

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    if not port:
        print("No serial port found. Usage: python serial_logger.py /dev/ttyUSB0")
        return

    current_file = None
    current_name = None

    while True:
        try:
            print(f"Connecting to {port} at {BAUD} baud...")
            ser = serial.Serial(port, BAUD, timeout=1)
            print("Connected. Waiting for ESP32...")

            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue

                print(line)

                if line.startswith("# session:"):
                    name = line.split("# session:")[-1].strip()
                    if current_file:
                        current_file.close()
                        print(f"[saved] {current_name}.csv")
                    current_name = name or "unnamed"
                    current_file = open(f"{current_name}.csv", "w")

                elif line == "timestamp_ms,raw":
                    if current_file:
                        current_file.write("timestamp_ms,raw\n")
                        current_file.flush()

                elif current_file and "," in line:
                    current_file.write(line + "\n")
                    current_file.flush()

        except KeyboardInterrupt:
            if current_file:
                current_file.close()
                print(f"\n[saved] {current_name}.csv")
            print("Done.")
            return

        except serial.SerialException:
            print("Disconnected. Reconnecting in 2s...")
            time.sleep(2)

if __name__ == "__main__":
    main()

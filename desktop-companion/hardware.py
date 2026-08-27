import serial
import serial.tools.list_ports
import json
import struct
import time
import threading
import queue
import logging

logger = logging.getLogger(__name__)
ESP32_VID = 0x1A86
KNOWN_CH340_PIDS = {0x7522, 0x7523, 0x5523}

connection = None
uart_buffer = ""

_write_lock = threading.Lock()
_write_queue = queue.Queue()

def _serial_writer():
    while True:
        item = _write_queue.get()

        try:
            if item is None:
                break

            is_large, data = item

            if connection is not None and connection.is_open:
                with _write_lock:
                    if is_large:
                        chunk_size = 128

                        for i in range(0, len(data), chunk_size):
                            chunk = data[i:i + chunk_size]
                            connection.write(chunk)
                            time.sleep(0.005)

                        connection.flush()

                        # Large image transfers are followed by a short delay
                        # so the ESP32 can decode/draw the JPEG before another
                        # UART packet fills its receive buffer.
                        time.sleep(0.4)
                    else:
                        connection.write(data)
                        connection.flush()

        except (serial.SerialException, OSError):
            logger.exception("Serial write failed; disconnecting ESP32")
            disconnect()

        finally:
            _write_queue.task_done()

threading.Thread(target=_serial_writer, daemon=True).start()

def find_esp32_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if port.vid == ESP32_VID and port.pid in KNOWN_CH340_PIDS:
            return port.device
    return None

def connect():
    global connection

    if connection and connection.is_open:
        return True

    port_name = find_esp32_port()

    if not port_name:
        return False

    try:
        connection = serial.Serial(port_name, 230400, timeout=1)
        logger.info("Connected to ESP32 on %s", port_name)
        return True
    except serial.SerialException:
        logger.exception("Failed to connect to ESP32 on %s", port_name)
        return False

def disconnect():
    global connection

    if connection:
        try:
            connection.close()
        except serial.SerialException:
            logger.exception("Error while closing ESP32 serial connection")
        finally:
            connection = None

def is_connected():
    return connection is not None and connection.is_open

def read_incoming():
    global uart_buffer
    connection
    if not is_connected():
        return []
    try:
        if connection.in_waiting > 0:
            uart_buffer += connection.read(connection.in_waiting).decode('utf-8', errors='ignore')
        if '\n' in uart_buffer:
            lines = uart_buffer.split('\n')
            uart_buffer = lines.pop()
            return [line.strip() for line in lines if line.strip()]
        return []
    except (serial.SerialException, OSError):
        logger.exception("Lost ESP32 serial connection while reading")
        disconnect()
        return []

def send_track_info(track_info):
    if not is_connected(): 
        return
    line = "T" + json.dumps(track_info) + "\n"
    _write_queue.put((False, line.encode('utf-8')))

def send_album_art(jpeg_bytes):
    if not is_connected(): 
        return
    header = b"I" + struct.pack('<I', len(jpeg_bytes))
    _write_queue.put((True, header + jpeg_bytes))

def send_voice_user_json(user_dict):
    if not is_connected(): 
        return
    line = f"U{json.dumps(user_dict)}\n"
    _write_queue.put((False, line.encode('utf-8')))

def send_avatar_image(index, jpeg_bytes):
    if not is_connected(): 
        return
    header = struct.pack('<c B I', b'A', index, len(jpeg_bytes))
    _write_queue.put((True, header + jpeg_bytes))

def send_discord_state(is_muted, is_deafened):
    if not is_connected(): 
        return
    line = f"D:{int(is_muted)}:{int(is_deafened)}\n"
    _write_queue.put((False, line.encode('utf-8')))

def send_voice_channel_name(name):
    if not is_connected(): 
        return
    line = f"N{name}\n"
    _write_queue.put((False, line.encode('utf-8')))
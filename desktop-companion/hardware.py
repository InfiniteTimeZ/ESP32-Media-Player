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
_ready_for_sync = False

_write_lock = threading.RLock()
_write_queue = queue.Queue()

_PACKET_MAGIC = b"\xA5\x5A"

def _build_packet(packet_type, payload=b""):
    if not isinstance(packet_type,bytes) or len(packet_type) != 1:
        raise ValueError("packet_type must be exactly one byte")

    return (_PACKET_MAGIC + packet_type + struct.pack("<I", len(payload)) + payload)


def request_ready():
    serial_connection = None

    try: 
        with _write_lock:
            serial_connection = connection

            if (serial_connection is None or not serial_connection.is_open):
                return False

            packet = _build_packet(b"R")
            serial_connection.write(packet)
            serial_connection.flush()
            return True
        
    except(serial.SerialException,OSError) as exc:
        logger.warning("Failed to request ESP32  ready state: %s", exc)
        disconnect(expected_connection=serial_connection)
        return False
    

def _clear_write_queue():
    while True:
        try:
            _write_queue.get_nowait()
            _write_queue.task_done()
        except queue.Empty:
            break


def _serial_writer():
    while True:
        item = _write_queue.get()
        serial_connection = None

        try:
            if item is None:
                break

            is_large, data = item

            if connection is not None and connection.is_open:
                with _write_lock:
                    serial_connection = connection

                    if serial_connection is None or not serial_connection.is_open:
                        continue

                    if is_large:
                        chunk_size = 128

                        for i in range(0, len(data), chunk_size):
                            chunk = data[i:i + chunk_size]
                            serial_connection.write(chunk)
                            time.sleep(0.005)

                        serial_connection.flush()

                        #Gives the ESP32 time to decode/draw large images before next packet
                        time.sleep(0.4)
                    else:
                        serial_connection.write(data)
                        serial_connection.flush()

        except (serial.SerialException, OSError) as exc:
            logger.exception("ESP32 serial connection lost: %s", exc)
            disconnect(expected_connection=serial_connection)
            

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
    global connection, uart_buffer, _ready_for_sync

    if connection and connection.is_open:
        return True

    port_name = find_esp32_port()

    if not port_name:
        return False

    with _write_lock:
        try:
            _ready_for_sync = False
            uart_buffer = ""
            _clear_write_queue()
            connection = serial.Serial(port_name, 230400, timeout=1)
            logger.info("Connected to ESP32 on %s", port_name)
            return True
        except serial.SerialException:
            logger.exception("Failed to connect to ESP32 on %s", port_name)
            return False

def disconnect(expected_connection=None):
    global connection, uart_buffer, _ready_for_sync

    with _write_lock:
        if expected_connection is not None and connection is not expected_connection:
            return

        _ready_for_sync = False
        _clear_write_queue()

        if connection:
            try:
                connection.close()
            except serial.SerialException:
                logger.exception("Error while closing ESP32 serial connection")
            finally:
                connection = None

        uart_buffer = ""

def is_connected():
    return connection is not None and connection.is_open

def mark_ready_for_sync():
    global _ready_for_sync
    _ready_for_sync = is_connected()

def is_ready_for_sync():
    return is_connected() and _ready_for_sync


def read_incoming():
    global uart_buffer
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
    except (serial.SerialException, OSError) as exc:
        logger.warning("ESP32 serial connection lost: %s", exc)
        disconnect()
        return []

def send_track_info(track_info):
    if not is_ready_for_sync(): 
        return
    
    payload = json.dumps(track_info).encode("utf-8")
    packet = _build_packet(b"T", payload)
    _write_queue.put((False, packet))

def send_album_art(jpeg_bytes):
    if not is_ready_for_sync(): 
        return
    logger.debug("Queueing album art: %d bytes", len(jpeg_bytes))
    packet = _build_packet(b"I", jpeg_bytes)
    _write_queue.put((True, packet))

def send_voice_user_json(user_dict):
    if not is_ready_for_sync(): 
        return

    payload = json.dumps(user_dict).encode("utf-8")
    packet = _build_packet(b"U", payload)
    _write_queue.put((False, packet))

def send_avatar_image(index, jpeg_bytes):
    if not is_ready_for_sync(): 
        return
    logger.debug( "Queueing Discord avatar %d: %d bytes", index, len(jpeg_bytes))
    payload = bytes([index]) + jpeg_bytes
    packet = _build_packet(b"A", payload)
    _write_queue.put((True,packet))

def send_discord_state(is_muted, is_deafened):
    if not is_ready_for_sync(): 
        return
    
    payload = bytes([int(is_muted), int(is_deafened)])
    packet = _build_packet(b"D", payload)
    _write_queue.put((False, packet))

def send_voice_channel_name(name):
    if not is_ready_for_sync(): 
        return

    payload = name.encode("ascii", errors="ignore")
    packet = _build_packet(b"N", payload)
    _write_queue.put((False, packet))
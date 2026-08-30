import asyncio
import os
import sys
import shutil
import time
import threading
from PIL import Image
import pystray
from pystray import MenuItem as item
import logging

import config
import network
import hardware
import media


is_paused = False
logger = logging.getLogger(__name__)

async def connect_hardware():
    if not hardware.connect():
        await asyncio.sleep(2)
        return False

    logger.info("Serial connected; waiting for ESP32 ready signal")

    deadline = time.monotonic() + 8
    next_probe = 0

    while time.monotonic() < deadline:
        now = time.monotonic()
                        
        #Retry slowly enough that an interrupted parser has time to reach
        # its 1.5 second stall recovery before next probe
        if now >= next_probe:
            hardware.request_ready()
            logger.info("Sent ESP32 readiness probe")
            next_probe = now + 2.0
            
        lines = hardware.read_incoming()
            
        if any("ESP32_READY" in line for line in lines):
            logger.info("ESP32 ready signal received")
            hardware.mark_ready_for_sync()
            network.resync_hardware()
            return True

        await asyncio.sleep(0.1)

    logger.warning("ESP32 ready signal was not received before timeout; reconnecting")

    hardware.disconnect()
    await asyncio.sleep(1)
    return False

       
async def handle_incoming_commands(session_manager, discord_rpc):
    incoming_commands = hardware.read_incoming()
    for clean_line in incoming_commands:
        logger.debug("UART received: %s", clean_line)

        if "Serial stall detected" in clean_line or "Corrupt stream detected" in clean_line  or "Serial packet timeout" in clean_line or "Invalid packet" in clean_line or "JPEG decode failed" in clean_line:

            logger.warning("ESP32 serial parser reported: %s", clean_line)
            continue

        if clean_line in ["CMD:NEXT", "CMD:PREV", "CMD:TOGGLE"] or clean_line.startswith("CMD:SEEK:"):
            await media.handle_media_command(session_manager, clean_line)

        elif clean_line == "CMD:MUTE":
            media.toggle_windows_mute()

        elif clean_line.startswith("CMD:VOL:"):
            try:
                volume = int(clean_line.split(":")[2])
                media.set_windows_volume(volume)
            except ValueError:
                logger.warning("Received invalid volume command: %s", clean_line)

        elif clean_line.startswith("CMD:TOGGLE_") or clean_line == "CMD:LV_CALL":
            asyncio.create_task(network.handle_discord_commands(discord_rpc, clean_line))


def configure_logging():
    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s | %(levelname)-8s | %(name)s | %(message)s",
        datefmt="%H:%M:%S",
    )



def track_signature(track_info):
    return (track_info.get("title"), track_info.get("artist"), track_info.get("album"), track_info.get("duration"), track_info.get("playback_status"), track_info.get("volume"), track_info.get("is_muted"), track_info.get("time"))

async def main_loop():
    global is_paused 

    hardware.start()
    media.start()

    
    last_processed_track = None
    last_resync_time = 0
    last_known_position = 0  
    last_sent_signature = None

    session_manager = await media.MediaManager.request_async()
    

    discord_rpc = None
    if config.app_config.get("use_discord"):
        discord_rpc = await network.init_discord_rpc()

    while True:
        if is_paused:
            hardware.disconnect()
            await asyncio.sleep(1)
            continue
                
        if not hardware.is_connected():
            if not await connect_hardware():
                continue
                     
            last_sent_signature = None
            last_processed_track = None
            last_resync_time = asyncio.get_running_loop().time()

        await handle_incoming_commands(session_manager, discord_rpc)

        
        track_info, last_processed_track, new_art = await media.get_current_track_info(session_manager, last_processed_track)

        if track_info:
            sig = track_signature(track_info)
            now = asyncio.get_event_loop().time()
            current_pos = track_info["position"]
            is_seek = abs(current_pos - last_known_position) > 3.0
            
            if sig != last_sent_signature:
                hardware.send_track_info(track_info)
                last_sent_signature = sig
                last_resync_time = now
            elif is_seek or (now - last_resync_time) > 5: 
                hardware.send_track_info(track_info)
                last_resync_time = now

            last_known_position = current_pos

        if new_art: 
            hardware.send_album_art(new_art)
            
        await asyncio.sleep(0.6)

def toggle_pause(icon, item):
    global is_paused
    is_paused = not item.checked

def quit_app(icon, item):
    icon.stop()
    os._exit(0)

def create_tray_icon():
    return Image.new('RGBA', (64, 64), color=(0, 255, 0, 255))

def start_asyncio_loop():
    asyncio.run(main_loop())

def setup_icon(icon):
    icon.visible = True

def toggle_discord(icon, item):
    if not item.checked:
        if not config.app_config.get("discord_client_id") or not config.app_config.get("discord_client_secret"):
            def enable_discord_callback():
                config.app_config["use_discord"] = not item.checked
                config.save_config()
                logger.info("Discord credentials saved; restart required")
            config.prompt_for_discord_credentials(enable_discord_callback)
            return
    config.app_config["use_discord"] = not item.checked
    config.save_config()
    logger.info("Discord integration setting updated; restart required")

def toggle_startup(icon, item):
    config.app_config["run_on_startup"] = not item.checked
    config.save_config()
    startup_folder = os.path.join(os.getenv('APPDATA'), r"Microsoft\Windows\Start Menu\Programs\Startup")
    exe_name = os.path.basename(sys.executable)
    startup_path = os.path.join(startup_folder, exe_name)
    if config.app_config["run_on_startup"]:
        try:
            shutil.copy(sys.executable, startup_path)
            logger.info("Enabled Windows startup")
        except Exception:
            logger.exception("Failed to enable Windows startup")
    else:
        if os.path.exists(startup_path):
            try:
                os.remove(startup_path)
                logger.info("Disabled Windows startup")
            except Exception:
                logger.exception("Failed to disable Windows startup")

if __name__ == "__main__":
    configure_logging() 

    threading.Thread(target=start_asyncio_loop, daemon=True).start()
    menu = pystray.Menu(
       item('Pause Sync', toggle_pause, checked=lambda item: is_paused),
        item('Enable Discord Integration', toggle_discord, checked=lambda item: config.app_config.get("use_discord")),
        item('Run on Windows Startup', toggle_startup, checked=lambda item: config.app_config.get("run_on_startup")),
        item('Quit', quit_app)
    )
    icon = pystray.Icon("ESP-Media", create_tray_icon(), "ESP32 Media Sync", menu)
    icon.run(setup_icon)
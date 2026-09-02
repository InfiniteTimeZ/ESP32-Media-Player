import io
import re
import queue
import asyncio
import threading
import unicodedata
import colorsys
import logging
import hashlib
from datetime import datetime, timezone
from PIL import Image

from winsdk.windows.media.control import GlobalSystemMediaTransportControlsSessionManager as MediaManager
from winsdk.windows.storage.streams import DataReader, Buffer, InputStreamOptions

last_known_color = [40, 40, 40]
volume_queue = queue.Queue()
current_windows_volume = -1
current_windows_mute = False
empty_session_count = 0
last_album_art_digest = None
_volume_thread = None
_ALBUM_ART_ATTEMPTS = 8
_ALBUM_ART_RETRY_DELAY = 0.15

logger = logging.getLogger(__name__)



def get_vibrant_color(img):
    r, g, b = img.resize((1, 1)).getpixel((0, 0))
    h, s, v = colorsys.rgb_to_hsv(r/255.0, g/255.0, b/255.0)
    s = min(1.0, s * 1.5)
    v = max(0.5, v)
    r, g, b = colorsys.hsv_to_rgb(h, s, v)
    return [int(r*255), int(g*255), int(b*255)]

def clean_text(text):
    if not text:
        return ""
    nfkd_form = unicodedata.normalize('NFKD', text)
    ascii_text = nfkd_form.encode('ascii', 'ignore').decode('utf-8')
    return re.sub(r'\s+', ' ', ascii_text).strip()

def _volume_worker():
    global current_windows_volume, current_windows_mute
    import comtypes
    from pycaw.pycaw import AudioUtilities
    comtypes.CoInitialize()
    
    # Initialize the device ONCE outside the loop to get a live, un-cached hardware pointer!
    try:
        device = AudioUtilities.GetSpeakers()
        volume = device.EndpointVolume
    except Exception:
        logger.exception("Windows volume controller failed to initialize")
        return

    while True:
       
        try:
            new_vol = int(volume.GetMasterVolumeLevelScalar() * 100)
            new_mute = bool(volume.GetMute())
            
            # Print a debug message so we can visibly verify Windows told Python!
            if new_mute != current_windows_mute:
                logger.debug("Windows mute state changed to %s", new_mute)
                
            current_windows_volume = new_vol
            current_windows_mute = new_mute

        except Exception:
             logger.exception("Failed to read Windows volume state")

        # 2. WRITE: Process any ESP32 volume/mute commands
        try:
            item = volume_queue.get(timeout=0.2)
            if item == "TOGGLE_MUTE":
                current_mute = volume.GetMute()
                volume.SetMute(not current_mute, None)
            else:
                volume.SetMasterVolumeLevelScalar(item / 100.0, None)
                volume.SetMute(0, None)
            volume_queue.task_done()
        except queue.Empty:
            pass


def start():
    global _volume_thread

    if _volume_thread is not None and _volume_thread.is_alive():
        return

    _volume_thread = threading.Thread(target=_volume_worker, name="windows-volume-worker", daemon=True)
    _volume_thread.start()

    logger.debug("Windows volume worker started")


def set_windows_volume(vol_percent):
    while not volume_queue.empty():
        try:
            volume_queue.get_nowait()
            volume_queue.task_done()
        except queue.Empty:
            break
    volume_queue.put(vol_percent)

def toggle_windows_mute():
    volume_queue.put("TOGGLE_MUTE")

def get_windows_volume():
    return current_windows_volume

def get_windows_mute():
    return current_windows_mute

async def get_album_art_image(thumbnail_ref):
    if thumbnail_ref is None:
        return None
    try:
        stream = await thumbnail_ref.open_read_async()
        size = stream.size
        if size == 0: return None
        buffer = Buffer(size)
        await stream.read_async(buffer, size, InputStreamOptions.READ_AHEAD)
        reader = DataReader.from_buffer(buffer)
        raw_bytes = bytearray(size)
        reader.read_bytes(raw_bytes)
        return Image.open(io.BytesIO(bytes(raw_bytes))).convert("RGB")
    except Exception as exc:
        logger.debug("Failed to read album artwork: %s", exc)
        return None

def resize_album_art(img, size=250):
    width, height = img.size
    min_side = min(width, height)
    left = (width - min_side) // 2
    top = (height - min_side) // 2
    img = img.crop((left, top, left + min_side, top + min_side))
    return img.resize((size, size), Image.LANCZOS)

def generate_blank_album_art(size=250):
    img = Image.new("RGB", (size, size), (40,40,40))
    img_byte_arr = io.BytesIO()
    img.save(img_byte_arr, format='JPEG', quality=85)
    return img_byte_arr.getvalue()

def image_to_jpeg_bytes(img):
    img = img.convert("RGB")
    clean_img = Image.new("RGB", img.size)
    clean_img.paste(img)
    img_byte_arr = io.BytesIO()
    clean_img.save(img_byte_arr, format='JPEG', quality=75)
    return img_byte_arr.getvalue()

def get_live_position(timeline, is_playing):
    if not is_playing:
        return timeline.position.total_seconds()

    now = datetime.now(timezone.utc)
    elapsed_since_update = (now - timeline.last_updated_time).total_seconds()
    estimated_position = (
        timeline.position.total_seconds() + elapsed_since_update
    )
    duration = timeline.end_time.total_seconds()
    return min(estimated_position, duration)

async def get_current_track_info(session_manager, last_track_id):
    global last_known_color, empty_session_count, last_album_art_digest
    current_session = session_manager.get_current_session()
    current_time = datetime.now().strftime("%I:%M %p").lstrip("0")

    if not current_session:
         empty_session_count += 1
         if empty_session_count < 3 and last_track_id != "NO_MEDIA":
             return None, last_track_id, None
             
         track_info = {
                     "title": "Awaiting Media...",
                     "artist": "PC Connected",
                     "album": "",
                     "duration": 0,
                     "position": 0,
                     "playback_status": "PAUSED",
                     "volume": get_windows_volume(),
                     "is_muted": get_windows_mute(),
                     "bg_color": [20,20,20],
                     "time": current_time
         }

         new_art = None
         if last_track_id != "NO_MEDIA":
             new_art = generate_blank_album_art()
             last_known_color = [20,20,20]
         return track_info, "NO_MEDIA", new_art

    empty_session_count = 0
    info = await current_session.try_get_media_properties_async()
    timeline = current_session.get_timeline_properties()
    playback_info = current_session.get_playback_info()

    get_current_track_id = f"{info.title} - {info.artist}"

    new_art = None
    if get_current_track_id != last_track_id:
        album_art = None
        selected_jpeg = None
        selected_digest = None

        await asyncio.sleep(0.25)

        for attempt in range(_ALBUM_ART_ATTEMPTS):  
            refreshed_info = await current_session.try_get_media_properties_async()
            
            if refreshed_info and refreshed_info.thumbnail:
                candidate = await get_album_art_image(refreshed_info.thumbnail)

                if candidate:
                    candidate = resize_album_art(candidate)
                    candidate_jpeg = image_to_jpeg_bytes(candidate)
                    candidate_digest = hashlib.sha1(candidate_jpeg).digest()

                    logger.debug("Album candidate: track=%r attempt=%d bytes=%d hash=%s", get_current_track_id, attempt + 1,  len(candidate_jpeg), hashlib.sha1(candidate_jpeg).hexdigest()[:8])

                    if( last_album_art_digest is None or candidate_digest != last_album_art_digest):
                        album_art = candidate
                        selected_jpeg = candidate_jpeg
                        selected_digest = candidate_digest
                        info = refreshed_info
                        break

                    album_art = candidate
                    selected_jpeg = candidate_jpeg
                    selected_digest = candidate_digest
 
                await asyncio.sleep(_ALBUM_ART_RETRY_DELAY)  

        if album_art:
            last_known_color = get_vibrant_color(album_art)
            if selected_digest != last_album_art_digest:
                new_art = selected_jpeg

            last_album_art_digest = selected_digest
        else:
            new_art = generate_blank_album_art()
            last_known_color = [40, 40, 40] 
            last_album_art_digest = hashlib.sha1(new_art).digest()

     
    is_playing = playback_info.playback_status.name == "PLAYING" if playback_info else False
    live_position = get_live_position(timeline, is_playing) if timeline else 0

    track_info = {
        "title": clean_text(info.title),
        "artist": clean_text(info.artist),
        "album": clean_text(info.album_title),
        "duration": timeline.end_time.total_seconds() if timeline else None,
        "position": live_position,
        "playback_status": playback_info.playback_status.name if playback_info else None,
        "volume": get_windows_volume(),
        "is_muted": get_windows_mute(),
        "bg_color": last_known_color,
        "time": current_time
    }

    return track_info, get_current_track_id, new_art

async def handle_media_command(session_manager, cmd):
    current_session = session_manager.get_current_session()
    if not current_session:
        return
        
    try:
        if cmd == "CMD:TOGGLE":
            await current_session.try_toggle_play_pause_async()
        elif cmd == "CMD:NEXT":
            await current_session.try_skip_next_async()
        elif cmd == "CMD:PREV":
            await current_session.try_skip_previous_async()
        elif cmd.startswith("CMD:SEEK:"):
            # ESP32 sends seconds; Windows SMTC requires 100-nanosecond ticks
            pos_sec = float(cmd.split(":")[2])
            ticks = int(pos_sec * 10000000)
            await current_session.try_change_playback_position_async(ticks)
    except Exception:
        logger.exception("Failed to execute media command: %s", cmd)
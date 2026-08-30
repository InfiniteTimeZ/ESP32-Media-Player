import json
import urllib.request
import urllib.parse
from pypresence import AioClient
import config
import time
from PIL import Image
import io
import asyncio
import hardware
import struct
import re
import unicodedata
import logging


_RECONCILE_INTERVAL = 1.0

active_channel_id = None
active_channel_name = ""
channel_users = {}
ipc_buffer = bytearray()
_last_command_time = {}
_COMMAND_DEBOUNCE_SECONDS = 0.3
_active_rpc_client = None
logger = logging.getLogger(__name__)

def _sanitize_display_text(text, fallback=""):
    """Return text that the ESP32's built-in Montserrat fonts can render."""
    if not text:
        return fallback

    normalized = unicodedata.normalize("NFKD", str(text))
    ascii_text = normalized.encode("ascii", "ignore").decode("ascii")
    ascii_text = re.sub(r"\s+", " ", ascii_text)
    printable_text = "".join(
        char for char in ascii_text if 0x20 <= ord(char) <= 0x7E
    ).strip()

    return printable_text or fallback

_avatar_bound_at_index = {}
_avatar_index_epoch = {}
_cached_jpeg = {}
_cached_jpeg_hash = {}

last_roster_ids = []

_rpc_lock = asyncio.Lock()
avatar_send_lock = asyncio.Lock()

def patch_pypresence(rpc):
    original_on_event = rpc.on_event

    def patched_on_event(data):
        global ipc_buffer
        ipc_buffer.extend(data)
        while True:
            if len(ipc_buffer) < 8:
                break
            opcode, length = struct.unpack('<II', ipc_buffer[:8])
            if len(ipc_buffer) < 8 + length:
                break
            single_packet = ipc_buffer[:8 + length]
            del ipc_buffer[:8 + length]
            try:
                original_on_event(bytes(single_packet))
            except Exception:
                logger.exception("Failed to process Discord IPC packet")

    rpc.on_event = patched_on_event

def _clear_avatar_bindings():
    _avatar_bound_at_index.clear()
    _avatar_index_epoch.clear()

def resync_hardware():
    hardware.send_voice_channel_name( active_channel_name if active_channel_id else "")
    global last_roster_ids
    if not active_channel_id:
        hardware.send_voice_user_json({"count": 0, "width": 0, "height": 0, "users": []})
        _clear_avatar_bindings()
        return
    last_roster_ids = []
    _clear_avatar_bindings()
    send_channel_users()

async def init_discord_rpc():
    global _active_rpc_client
    logger.info("Initializing Discord RPC")

    client_id = config.app_config.get("discord_client_id")
    client_secret = config.app_config.get("discord_client_secret")
    redirect_uri = config.app_config.get("discord_redirect_uri", "http://127.0.0.1")

    if not client_id or not client_secret:
        logger.warning("Discord credentials missing; Discord integration disabled")
        return None

    rpc = AioClient(client_id)
    _active_rpc_client = rpc
    patch_pypresence(rpc)

    try:
        await rpc.start()
        auth_response = await rpc.authorize(client_id, scopes=['rpc', 'rpc.voice.read', 'rpc.voice.write'])
        code = auth_response.get('code') or auth_response.get('data', {}).get('code')

        data = urllib.parse.urlencode({
            'client_id': client_id,
            'client_secret': client_secret,
            'grant_type': 'authorization_code',
            'code': code,
            'redirect_uri': redirect_uri
        }).encode('utf-8')

        req = urllib.request.Request('https://discord.com/api/oauth2/token', data=data)
        req.add_header('Content-Type', 'application/x-www-form-urlencoded')
        req.add_header('User-Agent', 'ESP32_Hardware_Controller/1.0')

        with urllib.request.urlopen(req) as response:
            token_data = json.loads(response.read().decode('utf-8'))
            access_token = token_data['access_token']

        await rpc.authenticate(access_token)

        logger.info("Discord RPC connected")
        asyncio.create_task(_reconciler_loop(rpc))
        return rpc
    except Exception as e:
        logger.exception("Discord RPC failed to start")
        return None

async def leave_discord_voice_channel(rpc_client):
    payload = {
        "cmd": "SELECT_VOICE_CHANNEL",
        "args": {"channel_id": None},
        "nonce": "{:.20f}".format(time.time())
    }
    rpc_client.send_data(1, payload)
    return await rpc_client.read_output()

async def handle_discord_commands(rpc_client, cmd):
    if not rpc_client:
        return

    now = time.time()
    if now - _last_command_time.get(cmd, 0) < _COMMAND_DEBOUNCE_SECONDS:
        return
    _last_command_time[cmd] = now

    try:
        if cmd == "CMD:TOGGLE_MUTE":
            async with _rpc_lock:
                voice_data = await rpc_client.get_voice_settings()
                current_mute = voice_data.get('mute') if 'mute' in voice_data else voice_data.get('data', {}).get('mute', False)
                current_deaf = voice_data.get('deaf') if 'deaf' in voice_data else voice_data.get('data', {}).get('deaf', False)
                new_mute = not current_mute
                await rpc_client.set_voice_settings(mute=new_mute)
                
            hardware.send_discord_state(new_mute,current_deaf)
            logger.info("Discord mute changed to %s", not current_mute)

        elif cmd == "CMD:TOGGLE_DEAFEN":
            async with _rpc_lock:
                voice_data = await rpc_client.get_voice_settings()
                current_mute = voice_data.get('mute') if 'mute' in voice_data else voice_data.get('data', {}).get('mute', False)
                current_deaf = voice_data.get('deaf') if 'deaf' in voice_data else voice_data.get('data', {}).get('deaf', False)
                new_deaf = not current_deaf
                await rpc_client.set_voice_settings(deaf=not current_deaf)

            hardware.send_discord_state(current_mute, new_deaf)
            logger.info("Discord deafen changed to %s", not current_deaf)

        elif cmd == "CMD:LV_CALL":
            async with _rpc_lock:
                await leave_discord_voice_channel(rpc_client)
            logger.info("Left Discord voice channel")

    except Exception as e:
        logger.exception("Discord command failed: %s", cmd)

def download_and_process_avatar(url, target_size):
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req) as response:
            raw_bytes = response.read()

        img = Image.open(io.BytesIO(raw_bytes))
        background = Image.new("RGB", img.size, (25, 25, 30))

        if img.mode in ('RGBA', 'LA') or (img.mode == 'P' and 'transparency' in img.info):
            rgba_image = img.convert('RGBA')
            background.paste(rgba_image, mask=rgba_image)
        else:
            background = img.convert("RGB")

        final_img = background.resize((target_size, target_size))

        out_buffer = io.BytesIO()
        final_img.save(out_buffer, format="JPEG", quality=85)

        return out_buffer.getvalue()
    except Exception as e:
        logger.warning("Avatar download failed: %s", exc)
        return None

def _layout_dimensions(user_count):
    if user_count <= 4:
        return 140, 150, 165
    elif user_count <= 8:
        return 110, 105, 120
    else:
        return 75, 85, 100

def send_channel_users():
    global user_id_to_index, last_roster_ids

    ordered_ids = list(channel_users.keys())[:15]
    user_id_to_index = {uid: idx for idx, uid in enumerate(ordered_ids)}
    user_count = len(ordered_ids)

    if user_count == 0:
        if last_roster_ids:
            hardware.send_voice_user_json({"count": 0, "width": 0, "height": 0, "users": []})
            last_roster_ids = []
            _clear_avatar_bindings()
        return 0

    target_size, card_w, card_h = _layout_dimensions(user_count)
    roster_changed = ordered_ids != last_roster_ids

    if roster_changed:
        last_roster_ids = ordered_ids.copy()

        user_json = {
            "count": user_count,
            "width": card_w,
            "height": card_h,
            "users": []
        }

        for uid in ordered_ids:
            user = channel_users[uid]
            display_name = _sanitize_display_text(user.get("name"), "Unknown")
            if len(display_name) > 12:
                display_name = display_name[:10] + ".."
            user_json["users"].append({
                "name": display_name
            })

        logger.info("Voice roster updated: %d users", user_count)
        hardware.send_voice_user_json(user_json)

    for idx, uid in enumerate(ordered_ids):
        user = channel_users[uid]
        ahash = user.get("avatar_hash")
        if not ahash:
            continue
        if _avatar_bound_at_index.get(idx) == (uid, ahash):
            continue
        _avatar_index_epoch[idx] = _avatar_index_epoch.get(idx, 0) + 1
        asyncio.create_task(_send_avatar(idx, uid, ahash, target_size, _avatar_index_epoch[idx]))

    return target_size

async def _send_avatar(idx, user_id, avatar_hash, target_size, epoch):
    jpeg_bytes = _cached_jpeg.get(user_id)
    if jpeg_bytes is None or _cached_jpeg_hash.get(user_id) != avatar_hash:
        await asyncio.sleep(0.5)
        url = f"https://cdn.discordapp.com/avatars/{user_id}/{avatar_hash}.png?size=128"
        jpeg_bytes = await asyncio.to_thread(download_and_process_avatar, url, target_size)
        if not jpeg_bytes:
            return
        _cached_jpeg[user_id] = jpeg_bytes
        _cached_jpeg_hash[user_id] = avatar_hash

    async with avatar_send_lock:
        if _avatar_index_epoch.get(idx) != epoch:
            return
        hardware.send_avatar_image(idx, jpeg_bytes)
        _avatar_bound_at_index[idx] = (user_id, avatar_hash)
        logger.debug(
            "Sent Discord avatar for index %d (%d bytes)",
             idx,
            len(jpeg_bytes),
        )
        await asyncio.sleep(0.05)

def _user_from_state(state):
    user_info = state.get("user", {})
    return user_info.get("id"), {
        "name": state.get("nick") or user_info.get("global_name") or user_info.get("username", "Unknown"),
        "avatar_hash": user_info.get("avatar"),
    }

def _switch_channel(new_channel_id, data):
    global active_channel_id, channel_users, last_roster_ids, active_channel_name

    active_channel_id = new_channel_id
    channel_users.clear()
    _clear_avatar_bindings()

    if not new_channel_id:
        active_channel_name = ""
        hardware.send_voice_channel_name("")
        send_channel_users()
        return

    last_roster_ids = []

    for state in (data.get("voice_states", []) if data else [])[:15]:
        uid, user = _user_from_state(state)
        if uid:
            channel_users[uid] = user

    active_channel_name = _sanitize_display_text(
        data.get("name") if data else None,
        "Voice Channel",
    )
    hardware.send_voice_channel_name(active_channel_name)

    logger.info(
        "Discord voice channel changed to %s with %d members",
        new_channel_id,
        len(channel_users),
    )
    send_channel_users()

def _apply_roster(voice_states):
    global channel_users
    new_users = {}
    for state in voice_states[:15]:
        uid, user = _user_from_state(state)
        if uid:
            new_users[uid] = user
    channel_users = new_users
    send_channel_users()

async def _reconcile(rpc_client):
    async with _rpc_lock:
        try:
            channel_fetch = await rpc_client.get_selected_voice_channel()
        except Exception as exc:
            logger.debug("Discord channel fetch failed: %s", exc)
            channel_fetch = None
        try:
            voice_settings = await rpc_client.get_voice_settings()
        except Exception as exc:
            logger.debug("Discord voice settings fetch failed: %s", exc)
            voice_settings = None

    data = channel_fetch.get("data") if channel_fetch else None
    new_channel_id = data.get("id") if data else None

    if new_channel_id != active_channel_id:
        _switch_channel(new_channel_id, data)
    elif new_channel_id:
        _apply_roster(data.get("voice_states", []))

    if voice_settings is not None:
        mute = voice_settings.get('mute') if 'mute' in voice_settings else voice_settings.get('data', {}).get('mute', False)
        deaf = voice_settings.get('deaf') if 'deaf' in voice_settings else voice_settings.get('data', {}).get('deaf', False)
        # Sent every tick rather than only on changes. If an ESP32 button
        # event is dropped or debounced and its local state drifts from
        # Discord's actual state, this corrects it on the next reconciliation.
        hardware.send_discord_state(mute, deaf)

async def _reconciler_loop(rpc_client):
    while _active_rpc_client is rpc_client:
        try:
            await _reconcile(rpc_client)
        except Exception as e:
           logger.exception("Unexpected error in Discord reconciliation loop")
        await asyncio.sleep(_RECONCILE_INTERVAL)
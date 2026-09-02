import io
import json
import logging
import struct
import time
import urllib.parse
import urllib.request

from PIL import Image
from pypresence import AioClient
import config



logger = logging.getLogger(__name__)
ipc_buffer = bytearray()



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
    except Exception as exc:
        logger.warning("Avatar download failed: %s", exc)
        return None


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


async def init_discord_rpc():
    logger.info("Initializing Discord RPC")

    client_id = config.app_config.get("discord_client_id")
    client_secret = config.app_config.get("discord_client_secret")
    redirect_uri = config.app_config.get("discord_redirect_uri", "http://127.0.0.1")

    if not client_id or not client_secret:
        logger.warning("Discord credentials missing; Discord integration disabled")
        return None

    rpc = AioClient(client_id)
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
        
        return rpc
    except Exception:
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

async def get_voice_state(rpc_client):
    voice_data = await rpc_client.get_voice_settings()

    muted = (voice_data.get("mute") if "mute" in voice_data else voice_data.get("data", {}).get("mute", False))
    deafened = ( voice_data.get("deaf") if "deaf" in voice_data else voice_data.get("data", {}).get("deaf", False))

    return muted, deafened

async def set_mute(rpc_client, muted):
    await rpc_client.set_voice_settings(mute=muted)

async def set_deafen(rpc_client, deafened):
    await rpc_client.set_voice_settings(deaf=deafened)

async def get_selected_voice_channel(rpc_client):
    return await rpc_client.get_selected_voice_channel()
import json
import logging
import os
import threading
import tkinter as tk
from tkinter import simpledialog

logger = logging.getLogger(__name__)

APPDATA_DIR  = os.getenv('APPDATA')
APP_FOLDER  = os.path.join(APPDATA_DIR, "ESP32_Media_Controller")
CONFIG_FILE = os.path.join(APP_FOLDER, "config.json")



app_config = {
    "use_discord": False,
    "run_on_startup": False,
    "discord_client_id": "",
    "discord_client_secret": "",
    "discord_redirect_uri": "http://127.0.0.1"
}

def load_config():
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r") as file:
                app_config.update(json.load(file))
        except (OSError, json.JSONDecodeError):
            logger.exception("Failed to load configuration from %s", CONFIG_FILE)

def save_config():
    os.makedirs(APP_FOLDER, exist_ok=True)

    try:
        with open(CONFIG_FILE, "w") as file:
            json.dump(app_config, file, indent=4)
    except OSError:
        logger.exception("Failed to save configuration to %s", CONFIG_FILE)


def prompt_for_discord_credentials(on_success):
    def _run_prompt():
        root = tk.Tk()
        root.withdraw()
        root.attributes('-topmost', True)

        root.after(100, root.focus_force)

        client_id = simpledialog.askstring("Discord Setup", "Enter your Discord Application (Client) ID:", parent=root)
        if not client_id:
            root.destroy()
            return

        client_secret = simpledialog.askstring("Discord Setup", "Enter your Discord Client Secret:", parent=root)
        if not client_secret:
            root.destroy()
            return

        redirect_uri = simpledialog.askstring("Discord Setup", "Enter Redirect URI (Leave alone if unsure):", initialvalue="http://127.0.0.1", parent=root)
        if not redirect_uri:
            redirect_uri = "http://127.0.0.1"

        app_config["discord_client_id"] = client_id.strip()
        app_config["discord_client_secret"] = client_secret.strip()
        app_config["discord_redirect_uri"] = redirect_uri.strip()
        save_config()

        root.destroy()

        on_success()

    threading.Thread(target=_run_prompt, name="discord-config-prompt", daemon=True).start()


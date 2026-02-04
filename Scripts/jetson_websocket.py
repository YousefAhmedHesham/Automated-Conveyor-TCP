# Jetson with websocket 
import socket
import json
import time
import random
import select
import threading
import queue
import os
import sys

# ---------------- CONFIGURATION ----------------
ESP_IP = "192.168.1.98"
ESP_PORT = 55001
LOCAL_SERVER_PORT = 65432

SIMULATE_CONGESTION = True
LAG_DELAY = 1.2
LAG_PROB = 0.2  # 20%

#BG_IMAGE_PATH = "/home/jetson/jetson_tcp_project/industrial-factory-with-network-connection.jpg"
BG_IMAGE_PATH = "/home/jetson/jetson_tcp_project/background.jpg"

# ---------------- GUI IMPORTS ----------------
import tkinter as tk

try:
    from PIL import Image, ImageTk
    PIL_OK = True
except Exception:
    PIL_OK = False


def send_ack(sock, seq: int):
    msg = json.dumps({"type": "ACK", "ack": int(seq)}, separators=(',', ':')) + "\n"
    sock.sendall(msg.encode())


# ======================================================
# GUI (DAQ / HMI WALL LAYOUT — LOGIC SAFE)
# ======================================================
class JetsonGatewayGUI:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("Jetson TCP Gateway Dashboard (Display Only)")
        self.root.geometry("1280x720")
        self.root.minsize(1100, 650)

        # ---------- COLORS ----------
        BG_PANEL = "#1b1b1b"
        BG_LOG = "#0f0f0f"
        FG_MAIN = "#e6e6e6"
        FG_MUTED = "#9a9a9a"
        FG_ACCENT = "#00c8ff"

        # ---------- CANVAS ----------
        self.canvas = tk.Canvas(root, highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)

        self.bg_image = None
        self.bg_imgtk = None

        # ---------- STATUS (TOP-LEFT) ----------
        self.status_panel = tk.Frame(self.canvas, bg=BG_PANEL)
        self.status_id = self.canvas.create_window(
            20, 20, anchor="nw", window=self.status_panel
        )

        self.lbl_esp = tk.Label(
            self.status_panel,
            text="ESP32: Disconnected",
            fg=FG_ACCENT,
            bg=BG_PANEL,
            font=("Segoe UI", 11, "bold"),
        )
        self.lbl_esp.pack(anchor="w", padx=15, pady=(10, 2))

        self.lbl_matlab = tk.Label(
            self.status_panel,
            text="MATLAB: Waiting",
            fg=FG_MUTED,
            bg=BG_PANEL,
            font=("Segoe UI", 11),
        )
        self.lbl_matlab.pack(anchor="w", padx=15, pady=(0, 10))

        # ---------- TELEMETRY (LEFT COLUMN) ----------
        self.telemetry_panel = tk.Frame(self.canvas, bg=BG_PANEL)
        self.telemetry_id = self.canvas.create_window(
            20, 120, anchor="nw", window=self.telemetry_panel
        )

        tk.Label(
            self.telemetry_panel,
            text="LIVE TELEMETRY",
            fg=FG_MUTED,
            bg=BG_PANEL,
            font=("Segoe UI", 10, "bold"),
        ).pack(anchor="w", padx=15, pady=(10, 5))

        grid = tk.Frame(self.telemetry_panel, bg=BG_PANEL)
        grid.pack(padx=15, pady=5)

        self.fields = {}
        labels = [
            "State", "Distance (cm)",
            "Speed (rpm)", "Items",
            "Interval (ms)", "RTT (ms)",
            "Congestion", "Encoder",
            "Limit", "IR",
        ]

        for i, key in enumerate(labels):
            r = i // 2
            c = i % 2

            tk.Label(
                grid,
                text=key,
                fg=FG_MUTED,
                bg=BG_PANEL,
                font=("Segoe UI", 9),
            ).grid(row=r, column=c * 2, sticky="e", padx=6, pady=4)

            val = tk.Label(
                grid,
                text="-",
                fg=FG_MAIN,
                bg=BG_PANEL,
                font=("Segoe UI", 11, "bold"),
                width=16,
                anchor="w",
            )
            val.grid(row=r, column=c * 2 + 1, sticky="w", padx=6, pady=4)
            self.fields[key] = val

        # ---------- TCP LOG (BOTTOM FULL WIDTH) ----------
        self.log_panel = tk.Frame(self.canvas, bg=BG_LOG)
        self.log_id = self.canvas.create_window(
            20, 420, anchor="nw", window=self.log_panel
        )

        tk.Label(
            self.log_panel,
            text="TCP EVENT LOG",
            fg=FG_MUTED,
            bg=BG_LOG,
            font=("Segoe UI", 10, "bold"),
        ).pack(anchor="w", padx=10, pady=(8, 4))

        self.txt = tk.Text(
            self.log_panel,
            height=10,
            bg=BG_LOG,
            fg="#d0d0d0",
            font=("Consolas", 9),
            wrap="none",
            borderwidth=0,
            insertbackground="white",
        )
        self.txt.pack(fill="both", expand=True, padx=10, pady=(0, 10))

        # ---------- RESIZE ----------
        self.root.bind("<Configure>", self._on_resize)
        self._load_background()

    # ---------- BACKGROUND ----------
    def _load_background(self):
        if not PIL_OK or not os.path.exists(BG_IMAGE_PATH):
            return
        self.bg_image = Image.open(BG_IMAGE_PATH).convert("RGB")
        self._render_background()

    def _render_background(self):
        if not self.bg_image:
            return
        w = self.root.winfo_width()
        h = self.root.winfo_height()
        img = self.bg_image.resize((w, h), Image.Resampling.LANCZOS)
        self.bg_imgtk = ImageTk.PhotoImage(img)
        self.canvas.delete("bg")
        self.canvas.create_image(0, 0, image=self.bg_imgtk, anchor="nw", tags="bg")

    def _on_resize(self, _=None):
        self._render_background()

    # ---------- UPDATE METHODS ----------
    def _log_line(self, s: str):
        self.txt.insert("end", s + "\n")
        self.txt.see("end")

    def set_esp_status(self, connected: bool):
        self.lbl_esp.config(
            text="ESP32: Connected" if connected else "ESP32: Disconnected"
        )

    def set_matlab_status(self, status_text: str):
        self.lbl_matlab.config(text=status_text.replace("?", ""))

    def update_status(self, pkt: dict):
        self.fields["State"].config(text=str(pkt.get("state", "-")))
        self.fields["Distance (cm)"].config(text=str(pkt.get("distance_cm", "-")))
        self.fields["Speed (rpm)"].config(text=str(pkt.get("speed_rpm", "-")))
        self.fields["Items"].config(text=str(pkt.get("items", "-")))
        self.fields["Interval (ms)"].config(text=str(pkt.get("interval", "-")))
        self.fields["RTT (ms)"].config(text=str(pkt.get("rtt", "-")))
        cong = pkt.get("congestion", "-")
        self.fields["Congestion"].config(
            text="YES" if cong == 1 else "NO" if cong == 0 else str(cong)
        )
        self.fields["Encoder"].config(text=str(pkt.get("encoder", "-")))
        self.fields["Limit"].config(text=str(pkt.get("limit", "-")))
        self.fields["IR"].config(text=str(pkt.get("ir", "-")))

    def update_fault(self, pkt: dict):
        pass  # faults stay in TCP log only


# ======================================================
# NETWORK — ORIGINAL LOGIC (UNTOUCHED)
# ======================================================
def network_worker(gui_queue: queue.Queue):
    def qlog(msg: str):
        gui_queue.put(("log", msg))

    def qconn(esp=None, matlab=None):
        if esp is not None:
            gui_queue.put(("esp_conn", esp))
        if matlab is not None:
            gui_queue.put(("matlab_conn", matlab))

    qlog("--- JETSON GATEWAY (TCP + GUI) ---")
    esp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    esp_sock.settimeout(5)

    try:
        esp_sock.connect((ESP_IP, ESP_PORT))
        qlog(f"Connected to ESP32 at {ESP_IP}:{ESP_PORT}")
        qconn(esp=True)
    except Exception as e:
        qlog(f"ESP connect error: {e}")
        qconn(esp=False)
        return

    matlab_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    matlab_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    matlab_server.bind(('0.0.0.0', LOCAL_SERVER_PORT))
    matlab_server.listen(1)

    qlog(f"Waiting for MATLAB on Port {LOCAL_SERVER_PORT}...")
    qconn(matlab="MATLAB: Waiting")

    matlab_conn, addr = matlab_server.accept()
    qlog(f"MATLAB Connected from {addr}")
    qconn(matlab="MATLAB: Connected")

    esp_sock.setblocking(False)
    matlab_conn.setblocking(False)

    esp_buffer = ""
    matlab_buffer = ""

    try:
        while True:
            rlist, _, _ = select.select([esp_sock, matlab_conn], [], [], 0.2)

            if matlab_conn in rlist:
                data = matlab_conn.recv(1024).decode(errors='ignore')
                if data:
                    matlab_buffer += data
                    while "\n" in matlab_buffer:
                        line, matlab_buffer = matlab_buffer.split("\n", 1)
                        line = line.strip()
                        if line:
                            esp_sock.sendall((line + "\n").encode())
                            qlog(f"TX CMD->ESP: {line}")

            if esp_sock in rlist:
                data = esp_sock.recv(1024).decode(errors='ignore')
                if not data:
                    continue

                esp_buffer += data
                while "\n" in esp_buffer:
                    line, esp_buffer = esp_buffer.split("\n", 1)
                    line = line.strip()
                    if not line:
                        continue

                    if line == "HELLO":
                        qlog("ESP says HELLO")
                        continue

                    try:
                        pkt = json.loads(line)
                    except json.JSONDecodeError:
                        qlog(f"RX (non-JSON): {line}")
                        continue

                    if "seq" in pkt and pkt.get("type") in ("STATUS", "FAULT"):
                        send_ack(esp_sock, pkt["seq"])

                    if SIMULATE_CONGESTION and random.random() < LAG_PROB:
                        qlog("*** SIMULATING LAG (after ACK) ***")
                        time.sleep(LAG_DELAY)

                    matlab_conn.sendall((json.dumps(pkt) + "\n").encode())

                    if pkt.get("type") == "STATUS":
                        gui_queue.put(("status", pkt))
                        qlog(f"RX STATUS seq={pkt.get('seq')} state={pkt.get('state')}")
                    elif pkt.get("type") == "FAULT":
                        qlog(f"RX FAULT code={pkt.get('code')}")

    finally:
        qconn(esp=False, matlab="MATLAB: Disconnected")


def main():
    root = tk.Tk()
    gui = JetsonGatewayGUI(root)

    gui_queue = queue.Queue()
    threading.Thread(target=network_worker, args=(gui_queue,), daemon=True).start()

    def poll_queue():
        try:
            while True:
                kind, payload = gui_queue.get_nowait()
                if kind == "log":
                    gui._log_line(payload)
                elif kind == "esp_conn":
                    gui.set_esp_status(bool(payload))
                elif kind == "matlab_conn":
                    gui.set_matlab_status(str(payload))
                elif kind == "status":
                    gui.update_status(payload)
        except queue.Empty:
            pass
        root.after(100, poll_queue)

    poll_queue()
    root.mainloop()


if __name__ == "__main__":
    main()
"""
Vimouse Remote Panel - 多机远程控制面板
双击启动，一键 SSH / 远程桌面
"""
import tkinter as tk
import subprocess
import socket
import json
import os
import threading
import webbrowser
import time

CONFIG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "remote-panel.json")

def _get_default_config():
    """根据机器名返回对应默认配置"""
    hostname = os.environ.get("COMPUTERNAME", "").upper()
    if hostname == "JINGYOUPC":
        # 公司电脑
        return {
            "ssh_key": "C:/Users/Admin/.ssh/win_remote",
            "pwsh": "C:\\Program Files\\PowerShell\\7\\pwsh.exe",
            "local_tunnel_port": 59124,
            "vimouse_port": 59123,
            "machines": [
                {
                    "name": "家里电脑",
                    "host": "100.83.179.15",
                    "user": "Administrator",
                    "color": "#2d5a2d",
                    "network": "tailscale"
                },
                {
                    "name": "Alpha Mac",
                    "host": "",
                    "user": "",
                    "color": "#5a2d5a",
                    "network": "tailscale",
                    "disabled": True
                },
                {
                    "name": "Release Mac",
                    "host": "",
                    "user": "",
                    "color": "#5a4a2d",
                    "network": "tailscale",
                    "disabled": True
                }
            ]
        }
    else:
        # 家里电脑 (默认)
        return {
            "ssh_key": "C:/Users/Administrator/.ssh/win_remote",
            "pwsh": "C:\\Program Files\\PowerShell\\7\\pwsh.exe",
            "local_tunnel_port": 59124,
            "vimouse_port": 59123,
            "machines": [
                {
                    "name": "公司电脑",
                    "host": "100.126.87.74",
                    "user": "Admin",
                    "color": "#2d5a2d",
                    "network": "tailscale"
                },
                {
                    "name": "公司电脑(VPN)",
                    "host": "172.16.140.54",
                    "user": "Admin",
                    "color": "#2d4a5a",
                    "network": "vpn"
                },
                {
                    "name": "Alpha Mac",
                    "host": "",
                    "user": "",
                    "color": "#5a2d5a",
                    "network": "tailscale",
                    "disabled": True
                },
                {
                    "name": "Release Mac",
                    "host": "",
                    "user": "",
                    "color": "#5a4a2d",
                    "network": "tailscale",
                    "disabled": True
                }
            ]
        }


def load_config():
    if os.path.exists(CONFIG_FILE):
        with open(CONFIG_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    # 首次运行，写入默认配置
    cfg = _get_default_config()
    save_config(cfg)
    return cfg


def save_config(cfg):
    with open(CONFIG_FILE, "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)


def is_port_listening(port):
    """检查本地端口是否已在监听"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(0.3)
        result = s.connect_ex(("127.0.0.1", port))
        s.close()
        return result == 0
    except:
        return False


def is_host_reachable(host, timeout=1):
    """通过 TCP 22 端口检测主机是否可达"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        result = s.connect_ex((host, 22))
        s.close()
        return result == 0
    except:
        return False


class RemotePanel:
    def __init__(self):
        self.cfg = load_config()
        self.root = tk.Tk()
        self.root.title("Vimouse Remote Panel")
        self.root.configure(bg="#1a1a1a")
        self.root.resizable(False, False)
        self.root.attributes("-topmost", True)

        self.status_labels = {}
        self.tunnel_proc = None

        self._build_ui()
        self._update_status()

    def _build_ui(self):
        # 标题
        tk.Label(
            self.root, text="Remote Panel", font=("Consolas", 14, "bold"),
            fg="#00ff00", bg="#1a1a1a"
        ).pack(pady=(10, 5))

        # 本机 Vimouse 状态
        self.local_label = tk.Label(
            self.root, text="", font=("Consolas", 9),
            fg="#888888", bg="#1a1a1a"
        )
        self.local_label.pack()

        # 机器列表
        for i, m in enumerate(self.cfg["machines"]):
            self._build_machine_row(i, m)

        # 底部工具栏
        bottom = tk.Frame(self.root, bg="#1a1a1a")
        bottom.pack(fill="x", padx=10, pady=(5, 10))

        tk.Button(
            bottom, text="刷新状态", command=self._update_status,
            bg="#333", fg="#0f0", font=("Consolas", 9),
            relief="flat", padx=8
        ).pack(side="left")

        tk.Button(
            bottom, text="编辑配置", command=self._open_config,
            bg="#333", fg="#aaa", font=("Consolas", 9),
            relief="flat", padx=8
        ).pack(side="left", padx=5)

        tk.Button(
            bottom, text="断开隧道", command=self._kill_tunnels,
            bg="#333", fg="#ff6666", font=("Consolas", 9),
            relief="flat", padx=8
        ).pack(side="right")

    def _build_machine_row(self, idx, m):
        disabled = m.get("disabled", False)
        frame = tk.Frame(self.root, bg=m.get("color", "#2a2a2a"), padx=8, pady=6)
        frame.pack(fill="x", padx=10, pady=3)

        # 名称 + 状态指示灯
        left = tk.Frame(frame, bg=frame["bg"])
        left.pack(side="left")

        status_dot = tk.Label(
            left, text="●", font=("Consolas", 10),
            fg="#555", bg=frame["bg"]
        )
        status_dot.pack(side="left")
        self.status_labels[idx] = status_dot

        tk.Label(
            left, text=f" {m['name']}", font=("Consolas", 11, "bold"),
            fg="#ddd" if not disabled else "#666", bg=frame["bg"]
        ).pack(side="left")

        if m.get("host"):
            tk.Label(
                left, text=f"  {m['user']}@{m['host']}", font=("Consolas", 9),
                fg="#888" if not disabled else "#555", bg=frame["bg"]
            ).pack(side="left")

        # 按钮
        right = tk.Frame(frame, bg=frame["bg"])
        right.pack(side="right")

        if not disabled:
            tk.Button(
                right, text="SSH", width=5,
                command=lambda i=idx: self._ssh(i),
                bg="#444", fg="#0f0", font=("Consolas", 9, "bold"),
                relief="flat"
            ).pack(side="left", padx=2)

            tk.Button(
                right, text="Desktop", width=7,
                command=lambda i=idx: self._desktop(i),
                bg="#444", fg="#ff0", font=("Consolas", 9, "bold"),
                relief="flat"
            ).pack(side="left", padx=2)
        else:
            tk.Label(
                right, text="未配置", font=("Consolas", 9),
                fg="#666", bg=frame["bg"]
            ).pack(side="left")

    def _ssh_cmd(self, m, extra_args=""):
        key = self.cfg["ssh_key"]
        return f'ssh -i {key} {extra_args}{m["user"]}@{m["host"]}'

    def _ssh(self, idx):
        m = self.cfg["machines"][idx]
        pwsh = self.cfg["pwsh"]
        cmd = self._ssh_cmd(m)
        subprocess.Popen([pwsh, "-NoExit", "-Command", cmd], creationflags=0x10)  # CREATE_NEW_CONSOLE

    def _desktop(self, idx):
        """SSH 隧道 + 打开浏览器"""
        m = self.cfg["machines"][idx]
        local_port = self.cfg["local_tunnel_port"]
        remote_port = self.cfg["vimouse_port"]

        def do():
            # 如果隧道已建立，直接打开浏览器
            if not is_port_listening(local_port):
                # 建立 SSH 隧道
                tunnel_args = f"-L {local_port}:127.0.0.1:{remote_port} -N"
                cmd = self._ssh_cmd(m, tunnel_args + " ")
                pwsh = self.cfg["pwsh"]
                subprocess.Popen([pwsh, "-NoExit", "-Command", cmd], creationflags=0x10)
                # 等待隧道建立
                for _ in range(30):
                    time.sleep(0.5)
                    if is_port_listening(local_port):
                        break

            webbrowser.open(f"http://127.0.0.1:{local_port}")
            self._update_status()

        threading.Thread(target=do, daemon=True).start()

    def _update_status(self):
        """异步刷新所有机器状态"""
        # 本机 Vimouse
        local_ok = is_port_listening(self.cfg["vimouse_port"])
        tunnel_ok = is_port_listening(self.cfg["local_tunnel_port"])
        parts = []
        parts.append(f"本机 Vimouse: {'✓ :' + str(self.cfg['vimouse_port']) if local_ok else '✗'}")
        parts.append(f"隧道: {'✓ :' + str(self.cfg['local_tunnel_port']) if tunnel_ok else '—'}")
        self.local_label.config(text="  |  ".join(parts))

        def check(idx, m):
            if m.get("disabled") or not m.get("host"):
                return
            ok = is_host_reachable(m["host"])
            self.root.after(0, lambda: self.status_labels[idx].config(
                fg="#00ff00" if ok else "#ff3333"
            ))

        for i, m in enumerate(self.cfg["machines"]):
            threading.Thread(target=check, args=(i, m), daemon=True).start()

    def _kill_tunnels(self):
        """杀掉所有 SSH 隧道进程（占用 local_tunnel_port 的）"""
        port = self.cfg["local_tunnel_port"]
        try:
            result = subprocess.run(
                ["netstat", "-ano"], capture_output=True, text=True
            )
            for line in result.stdout.splitlines():
                if f":{port}" in line and "LISTENING" in line:
                    pid = line.strip().split()[-1]
                    subprocess.run(["taskkill", "/PID", pid, "/F"],
                                   capture_output=True)
        except:
            pass
        self._update_status()

    def _open_config(self):
        os.startfile(CONFIG_FILE)

    def run(self):
        self.root.mainloop()


if __name__ == "__main__":
    RemotePanel().run()

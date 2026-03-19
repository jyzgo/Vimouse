"""
Wake-on-LAN 唤醒工具
用法: python wol.py [MAC地址]
默认唤醒家里电脑 (ifpc)
"""
import socket
import sys

TARGETS = {
    "ifpc": "E8:FB:1C:7E:BA:F9",
}

def send_wol(mac_str, broadcast="255.255.255.255", port=9):
    mac = mac_str.replace(":", "").replace("-", "")
    if len(mac) != 12:
        print(f"Invalid MAC: {mac_str}")
        return False
    mac_bytes = bytes.fromhex(mac)
    magic = b'\xff' * 6 + mac_bytes * 16
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        s.sendto(magic, (broadcast, port))
    print(f"WoL magic packet sent to {mac_str}")
    return True

if __name__ == "__main__":
    if len(sys.argv) > 1:
        mac = sys.argv[1]
    else:
        mac = TARGETS.get("ifpc", "")
    if mac:
        send_wol(mac)
    else:
        print("Usage: python wol.py <MAC>")
        print(f"Known targets: {TARGETS}")

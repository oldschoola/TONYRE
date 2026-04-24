def thug_crc(s: str) -> int:
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
        table.append(c)
    rc = 0xFFFFFFFF
    for ch in s:
        if 'A' <= ch <= 'Z':
            ch = ch.lower()
        elif ch == '/':
            ch = '\\'
        rc = table[(rc ^ ord(ch)) & 0xFF] ^ ((rc >> 8) & 0x00FFFFFF)
    return rc & 0xFFFFFFFF

candidates = [
    "Aceyalone",
    "aceyalone",
    "music\\vag\\songs\\Aceyalone",
    "vag\\songs\\Aceyalone",
    "songs\\Aceyalone",
    "streams\\music\\Aceyalone",
    "Data\\music\\Aceyalone",
    "music\\Aceyalone",
    "Aceyalone.bik",
    "Aceyalone.wav",
    "music\\vag\\songs\\Aceyalone.wav",
    "music\\vag\\songs\\Aceyalone.bik",
    "music/vag/songs/Aceyalone",
]
known_biks = {
    "04c57944", "0986163b", "09d4f945", "0b4c136d", "0b8604dc",
}
for v in candidates:
    h = f"{thug_crc(v):08x}"
    print(f"{h}  {'(!)' if h in known_biks else '   '} {v}")

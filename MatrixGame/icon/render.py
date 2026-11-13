import io, os, re, struct, subprocess, sys

# Renders icon.svg (64 px and up) and icon-small.svg (16-48 px), both from icon.png, with Microsoft Edge into
# ../MatrixGame.ico and ../small.ico. Usage: python render.py <a scratch folder>
SRC = os.path.dirname(os.path.abspath(__file__)) + '/'
OUT = os.path.abspath(sys.argv[1])
os.makedirs(OUT, exist_ok=True)
EDGE = r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
HERE = 'file:///' + SRC.replace('\\', '/')  # the pictures the drawings take, by their full address


def render(size):
    name = 'icon-small.svg' if size <= 48 else 'icon.svg'
    svg = io.open(SRC + name, encoding='utf-8').read()
    svg = re.sub(r'width="\d+" height="\d+"', 'width="%d" height="%d"' % (size, size), svg, count=1)
    svg = re.sub(r'href="([\w.-]+\.png)"', lambda m: 'href="%s%s"' % (HERE, m.group(1)), svg)
    page = os.path.join(OUT, 'p%d.html' % size)
    io.open(page, 'w', encoding='utf-8').write(
        '<!doctype html><html><body style="margin:0;background:transparent;overflow:hidden">'
        + svg.replace('<svg ', '<svg style="display:block" ') + '</body></html>')
    png = os.path.join(OUT, '%d.png' % size)
    if os.path.exists(png):
        os.remove(png)
    subprocess.run([EDGE, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', '--no-default-browser-check', '--force-device-scale-factor=1',
                    '--default-background-color=00000000', '--window-size=%d,%d' % (size, size),
                    '--user-data-dir=' + os.path.join(OUT, 'edge'), '--screenshot=' + png,
                    'file:///' + page.replace('\\', '/')], check=True, capture_output=True, timeout=180)
    data = open(png, 'rb').read()
    w, h = struct.unpack('>II', data[16:24])
    if (w, h) != (size, size):
        raise SystemExit('%s is %dx%d' % (png, w, h))
    return data


def pack(entries, path):
    head = struct.pack('<HHH', 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    dirs, blobs = b'', b''
    for size, data in entries:
        dirs += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
        blobs += data
    open(path, 'wb').write(head + dirs + blobs)


images = [(s, render(s)) for s in SIZES]
pack(images, SRC + '../MatrixGame.ico')
pack([i for i in images if i[0] in (16, 20, 24, 32)], SRC + '../small.ico')
print('ok', [len(d) for _, d in images])

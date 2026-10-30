"""Deterministic terminal text that redraws on resize; no personal shell content."""
import os,signal,sys
last=None
def draw(*_):
    global last
    columns,rows=os.get_terminal_size(sys.stdout.fileno())
    size=(columns,rows)
    if size==last:return
    last=size
    columns=min(columns,1200);rows=min(rows,400)
    text='def render_frame(window, viewport): return window.contents  # synthetic terminal text  '
    lines=[]
    for y in range(max(1,rows-1)):
        line=(f'{y:04d}  '+text*((columns//len(text))+1))[:max(1,columns-1)]
        lines.append(f'\x1b[38;5;{80+y%8}m'+line)
    sys.stdout.write('\x1b[?25l\x1b[H\x1b[2J'+'\r\n'.join(lines)+'\x1b[0m')
    sys.stdout.flush()
signal.signal(signal.SIGWINCH,draw)
draw()
while True:signal.pause()

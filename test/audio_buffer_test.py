"""Model the configured mono buffer against burst/gap traces; no audio quality claim."""
from pathlib import Path
import re
source=(Path(__file__).resolve().parents[1]/'audio/src/main.cpp').read_text()
capacity=int(re.search(r'set_i2s_ringbuffer_size\((\d+)\)',source)[1]);percent=int(re.search(r'set_i2s_ringbuffer_prefetch_percent\((\d+)\)',source)[1])
threshold=capacity*percent//100//4*4;prefill=(threshold+1023)//1024*1024
# Four adjacent 4096B stereo callbacks add 8192B mono before a consumer runs.
assert prefill+8192<=capacity
# 110ms arrival gap at 48kHz, after the small DMA reserve is ignored.
assert prefill>=96000*110//1000
old_capacity,old_fill=12288,10240
assert old_fill+8192>old_capacity and old_fill<96000*110//1000
print(f'Configured mono ring {capacity}B, prefill {prefill}B: 4-callback burst and 110ms gap model PASS (old 12KB case fails); hardware timing still required')

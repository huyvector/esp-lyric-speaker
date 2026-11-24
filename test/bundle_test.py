from pathlib import Path
import importlib.util
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('bundle',root/'tools/bundle.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
a=bytearray(1000);a[0]=233;c=a.copy();c[12]=5
x=b.pack(a,c);assert(b.inspect(x)['audio']==1000)
for data in [x[:-1],x+b'x',x[:300]+b'bad'+x[303:],b'bad'+x[3:]]:
 try: b.inspect(data)
 except ValueError: pass
 else: raise AssertionError('bad bundle accepted')
try: b.pack(c,a)
except ValueError: pass
else: raise AssertionError('swapped targets accepted')
print('Bundle: correct pair, truncated/extra/corrupt payload, wrong chip rejection: PASS')

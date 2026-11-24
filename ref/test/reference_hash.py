# Independent integer reference, including upstream Q32.32 xorshift/extraction.
from pathlib import Path

def digest(data,h,f,domain=None):
    # Construct the frame independently; C reads virtual words without allocating.
    if domain is None: domain = {256: 1, 512: 2}[h]
    data = (b"CHF\x01" + domain.to_bytes(4,"little") + data +
            b"\0" * (-len(data) % 8) + len(data).to_bytes(8,"little"))
    scale=1<<f;mask=scale-1
    zr=int(.4569*scale);zi=int(.5133*scale)
    cr=int(.7429*scale);ci=int(.2142*scale)
    alpha=int((10.45+h/10000.0)*scale)
    def mul(a,b):return a*b//scale
    def mix(value):
        if f == 24: return value
        value ^= (value << 13) & mask
        value ^= value >> 17
        value ^= (value << 5) & mask
        return value
    def step(zr,zi,cr,ci):
        return (mix((mul(mul(alpha,alpha),mul(zr,zr)-mul(zi,zi))+cr)&mask),
                mix((mul(mul(alpha,alpha),2*mul(zr,zi))+ci)&mask))
    if data:
        for offset in range(0,len(data),8):
            a=int.from_bytes(data[offset:offset+4],'little')
            b=int.from_bytes(data[offset+4:offset+8],'little') if offset+4<len(data) else 1
            # Direct rational equation, rather than the C shift shortcut.
            cr=((a+1)*scale)//((1<<32)+1)
            ci=((b+1)*scale)//((1<<32)+1)
            zr,zi=step(zr,zi,cr,ci)
        for _ in range(h+200):zr,zi=step(zr,zi,cr,ci)
    out=bytearray()
    for _ in range(h//32):
        zr,zi=step(zr,zi,cr,ci)
        for v in (zr,zi):out.extend(((v>>8)&65535).to_bytes(2,'big'))
    return bytes(out)



def compare_library():
    import argparse
    import ctypes
    import hashlib
    parser = argparse.ArgumentParser(description='Compare the C hash with independent integer equations.')
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--precision', type=int, choices=(24, 32), default=32)
    args = parser.parse_args()
    lib = ctypes.CDLL(str(args.library.resolve()))
    checks = 0
    for size in (0, 1, 2, 3, 4, 5, 7, 8, 9, 16, 31, 32, 33, 63, 64, 65, 129):
        for case in range(4):
            data = hashlib.shake_256(bytes([case, size])).digest(size)
            source = (ctypes.c_uint8 * max(1, size))()
            if size: ctypes.memmove(source, data, size)
            for bits in (256, 512):
                actual = (ctypes.c_uint8 * (bits // 8))()
                function = getattr(lib, f'_sha3_{bits}')
                function.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
                function.restype = None
                function(actual, source, size)
                expected = digest(data, bits, args.precision)
                if bytes(actual) != expected:
                    raise SystemExit(f'FAIL: Q{args.precision}, bytes={size}, case={case}, hash={bits}')
                checks += 1
    print(f'PASS: {checks} comparisons against independent Q{args.precision} integer equations')


if __name__ == "__main__":
    compare_library()

#!/usr/bin/env python3
"""emit the assembly that embeds a family's GMU, SQE and zap firmware as the strong symbols NativeAdreno looks up
(native_adreno_firmware_image, native_adreno_sqe_firmware_image, native_adreno_zap_image, each with a _size), the way
the sheng kit builder does it, from a pin file such as a650-firmware.json and a directory holding the vendor files.

every input is checked against its pinned size and sha256 before anything is written, and nothing here enters git: the
pin file holds hashes only. a split zap (a .mdt with its .bNN segments, as the a650 ships) is assembled into one image
with each segment at its p_offset, which is the layout AdrenoZap.c and the board loader expect; a single .mbn is used as
is. usage: AdrenoFirmwareEmbed.py --pins PINS --firmware-dir DIR --output firmware.S [--check]"""
import hashlib, json, struct, sys
from pathlib import Path


def argument(name, default=None):
    if name in sys.argv[1:]:
        return sys.argv[sys.argv.index(name) + 1]
    if default is None:
        raise SystemExit(name + ' is required')
    return default


def pinned(directory, pin):
    path = directory / pin['file']
    if not path.is_file():
        raise SystemExit('firmware ' + pin['file'] + ' is missing')
    data = path.read_bytes()
    if len(data) != pin['bytes'] or hashlib.sha256(data).hexdigest() != pin['sha256']:
        raise SystemExit('firmware ' + pin['file'] + ' does not match its pin')
    return data


def assemble_zap(metadata, segments):
    """the .mdt holds the ELF header, program headers and hash segment; each loadable segment file lands at its p_offset"""
    phoff, = struct.unpack_from('<I', metadata, 28)
    phentsize, phnum = struct.unpack_from('<HH', metadata, 42)
    if metadata[:4] != b'\x7fELF' or phentsize != 32:
        raise SystemExit('the zap metadata is not an ELF32 image')
    headers = [struct.unpack_from('<8I', metadata, phoff + i * 32) for i in range(phnum)]
    image = bytearray(metadata)
    loadable = [h for h in headers if h[0] == 1 and ((h[6] >> 24) & 7) != 2 and h[5]]
    if len(loadable) != len(segments):
        raise SystemExit('%d loadable segments in the metadata, %d segment files pinned' % (len(loadable), len(segments)))
    for header, data in zip(loadable, segments):
        offset, filesz = header[1], header[4]
        if len(data) != filesz:
            raise SystemExit('segment file size differs from p_filesz')
        if len(image) < offset + filesz:
            image.extend(b'\0' * (offset + filesz - len(image)))
        image[offset:offset + filesz] = data
    return bytes(image)


def main():
    pins = json.loads(Path(argument('--pins')).read_text())
    directory = Path(argument('--firmware-dir'))
    output = Path(argument('--output'))
    output.parent.mkdir(parents=True, exist_ok=True)
    images = {}
    for key in ('gmu', 'sqe'):
        images[pins[key]['symbol']] = pinned(directory, pins[key])
    zap = pins['zap']
    if 'segments' in zap:
        images[zap['symbol']] = assemble_zap(pinned(directory, zap['metadata']), [pinned(directory, s) for s in zap['segments']])
    else:
        images[zap['symbol']] = pinned(directory, zap['file'])
    if '--check' in sys.argv[1:]:
        print('PASS: %s firmware inputs match their pins' % pins['family'])
        return
    lines = ['.section __DATA_CONST,__const']
    for symbol, data in images.items():
        blob = output.with_name(symbol + '.bin')
        blob.write_bytes(data)
        lines += ['.p2align 12', '.globl _' + symbol, '_' + symbol + ':', '.incbin ' + json.dumps(str(blob)), '_' + symbol + '_end:',
                  '.p2align 3', '.globl _' + symbol + '_size', '_' + symbol + '_size:', '.quad _' + symbol + '_end-_' + symbol]
    output.write_text('\n'.join(lines) + '\n')
    print('PASS: %s firmware (%s) written to %s' % (pins['family'], ', '.join('%s %d bytes' % (s, len(d)) for s, d in images.items()), output))


main()

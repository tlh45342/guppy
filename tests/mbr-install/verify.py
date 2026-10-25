from pathlib import Path
import sys
img = Path('mbr-install.img').read_bytes()
boot = Path('boot.bin').read_bytes()
before = Path('lba0-before.bin').read_bytes()
if img[:len(boot)] != boot:
    raise SystemExit('FAIL installed bootstrap bytes differ from boot.bin')
if any(img[len(boot):446]):
    raise SystemExit('FAIL bootstrap tail was not zero-filled')
if img[446:512] != before[446:512]:
    raise SystemExit('FAIL bytes 446..511 changed')
if img[450] != 0xEE:
    raise SystemExit('FAIL protective MBR type 0xEE not preserved')
if img[510:512] != b'\x55\xaa':
    raise SystemExit('FAIL MBR signature not preserved')
log = Path('mbr-install-reopen.log').read_text(errors='replace')
if 'linux' not in log or '55 aa' not in log.lower():
    raise SystemExit('FAIL reopened GPT/bread evidence missing')
print('[VERIFY] mbr install bootstrap replacement and 446..511 preservation PASS')
print('[VERIFY] reopened GPT and LBA 0 inspection PASS')

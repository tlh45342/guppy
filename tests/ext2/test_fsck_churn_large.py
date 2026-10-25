"""Seeded ext2 churn with direct/single/double-indirect files and fsck checkpoints."""
from pathlib import Path
import hashlib,random,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
EXE=ROOT/'bin'/'guppy.exe'
SEED=20261023
# Guppy's mkfs uses 4KiB blocks: direct=48KiB, single-indirect ~4MiB.
SIZES=[0,1,4095,4096,4097,49151,49152,49153,65536,1024*1024,
       4*1024*1024+49152-1,4*1024*1024+49152+1,5*1024*1024]
def run(wd,commands):
    (wd/'large.script').write_text('\n'.join(commands)+'\n',encoding='utf8')
    return subprocess.run([str(EXE),'large.script'],cwd=wd,capture_output=True,text=True)
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()
with tempfile.TemporaryDirectory(prefix='fsck-large-',dir=ROOT/'tests'/'ext2') as t:
    wd=Path(t)
    setup=run(wd,['create churn.img --size 128MiB','use -i churn.img /dev/a',
        'gpt init /dev/a','gpt add /dev/a --type linuxfs --name rootfs --start 1MiB --size 100%',
        'partscan /dev/a','mkfs.ext2 /dev/a1'])
    assert setup.returncode==0,(setup.stdout,setup.stderr)
    rng=random.Random(SEED)
    live={}
    total=0
    for batch in range(8):
        commands=['use -i churn.img /dev/a','partscan /dev/a','mount /dev/a1 /']
        for j in range(16):
            if live and rng.random()<0.52:
                name=rng.choice(list(live))
                commands.extend([f'stat /{name}',f'rm /{name}'])
                del live[name]
            else:
                name=f'f{batch:02d}_{j:02d}.dat'
                size=SIZES[(batch*16+j)%len(SIZES)]
                # Stream predictable random bytes without a large temporary allocation.
                with (wd/name).open('wb') as f:
                    remaining=size
                    while remaining:
                        n=min(remaining,65536)
                        f.write(rng.randbytes(n))
                        remaining-=n
                commands.append(f'lcp {name} /{name}')
                live[name]=size
            total+=1
        result=run(wd,commands)
        if result.returncode:
            raise AssertionError(f'batch={batch} seed={SEED} operations={commands}\nstdout={result.stdout[-3000:]}\nstderr={result.stderr[-3000:]}')
        image=wd/'churn.img'
        before=sha(image)
        check=run(wd,['use -i churn.img /dev/a','partscan /dev/a','fsck.ext2 /dev/a1'])
        assert check.returncode==0 and 'errors=0' in check.stdout,(batch,check.stdout,check.stderr)
        assert sha(image)==before, 'read-only fsck changed image'
        print(f'PASS batch {batch+1}/8 operations={total} live={len(live)}')
    print(f'PASS large ext2 churn: {total} operations seed={SEED}, 8 fsck checkpoints, SHA256 read-only')

"""Read-only ext2 multi-group accounting and full reclamation regression."""
from pathlib import Path
import hashlib,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
EXE=ROOT/'bin'/'guppy.exe'
def run(wd,commands):
    (wd/'multi.script').write_text('\n'.join(commands)+'\n',encoding='utf8')
    return subprocess.run([str(EXE),'multi.script'],cwd=wd,capture_output=True,text=True)
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()
def check(wd):
    path=wd/'multi.img'
    before=digest(path)
    p=run(wd,['use -i multi.img /dev/a','partscan /dev/a','fsck.ext2 /dev/a1'])
    assert p.returncode==0 and 'errors=0' in p.stdout,(p.stdout,p.stderr)
    assert digest(path)==before,'fsck modified image'
    groups=re.search(r'groups=(\d+)',p.stdout)
    assert groups and int(groups.group(1))>=2,p.stdout
    counts=re.search(r'free blocks=(\d+) free inodes=(\d+)',p.stdout)
    assert counts,p.stdout
    return tuple(map(int,counts.groups()))
with tempfile.TemporaryDirectory(prefix='fsck-multigroup-',dir=ROOT/'tests'/'ext2') as t:
    wd=Path(t)
    setup=run(wd,['create multi.img --size 256MiB','use -i multi.img /dev/a',
      'gpt init /dev/a','gpt add /dev/a --type linuxfs --name rootfs --start 1MiB --size 100%',
      'partscan /dev/a','mkfs.ext2 /dev/a1'])
    assert setup.returncode==0,(setup.stdout,setup.stderr)
    baseline=check(wd)
    # 4KiB ext2 group size is normally 128MiB; fill past the first group.
    names=[]
    for batch in range(3):
        commands=['use -i multi.img /dev/a','partscan /dev/a','mount /dev/a1 /']
        for j in range(12):
            name=f'g{batch}_{j}.dat'
            src=wd/name
            # Use deterministic 5MiB data; total 180MiB across 36 files.
            with src.open('wb') as f:
                for k in range(80):f.write(bytes([(batch*17+j*7+k)&255])*65536)
            commands.append(f'lcp {name} /{name}')
            names.append(name)
        p=run(wd,commands)
        assert p.returncode==0,(batch,p.stdout[-1000:],p.stderr[-1000:])
        current=check(wd)
        print(f'PASS allocation batch {batch+1}/3 free={current}')
    for batch in range(3):
        commands=['use -i multi.img /dev/a','partscan /dev/a','mount /dev/a1 /']
        for name in names[batch*12:(batch+1)*12]:commands.append(f'rm /{name}')
        p=run(wd,commands)
        assert p.returncode==0,(batch,p.stdout[-1000:],p.stderr[-1000:])
        current=check(wd)
        print(f'PASS reclamation batch {batch+1}/3 free={current}')
    assert current==baseline,(baseline,current)
    print('PASS multi-group 180MiB create/delete, accounting restored to baseline, fsck read-only')

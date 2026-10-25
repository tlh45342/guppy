"""Deterministic create/delete workload on a disposable ext2 image; read-only fsck after each batch."""
from pathlib import Path
import hashlib, random, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
GUPPY=ROOT/'bin'/'guppy.exe'
def run(wd, commands):
    (wd/'churn.script').write_text('\n'.join(commands)+'\n',encoding='utf-8')
    return subprocess.run([str(GUPPY),'churn.script'],cwd=wd,capture_output=True,text=True)
def verify(wd):
    image=wd/'churn.img'
    before=hashlib.sha256(image.read_bytes()).hexdigest()
    result=run(wd,['use -i churn.img /dev/a','partscan /dev/a','fsck.ext2 /dev/a1'])
    assert result.returncode==0 and 'errors=0' in result.stdout,(result.stdout,result.stderr)
    assert hashlib.sha256(image.read_bytes()).hexdigest()==before,'fsck modified image'
with tempfile.TemporaryDirectory(prefix='fsck-churn-',dir=ROOT/'tests'/'ext2') as tmp:
    wd=Path(tmp)
    setup=run(wd,['create churn.img --size 32MiB','use -i churn.img /dev/a',
                  'gpt init /dev/a','gpt add /dev/a --type linuxfs --name rootfs --start 1MiB --size 100%',
                  'partscan /dev/a','mkfs.ext2 /dev/a1'])
    assert setup.returncode==0,(setup.stdout,setup.stderr)
    verify(wd)
    rng=random.Random(20261023)
    live={}
    for batch in range(3):
        commands=['use -i churn.img /dev/a','partscan /dev/a','mount /dev/a1 /']
        for j in range(8):
            if live and rng.random()<0.45:
                name=rng.choice(list(live))
                commands.append('ls /')
                commands.append(f'stat /{name}')
                commands.append(f'rm /{name}')
                del live[name]
            else:
                name=f'f{batch}_{j}.dat'
                size=rng.choice([0,1,100,4096,50000,90000])
                (wd/name).write_bytes(rng.randbytes(size))
                commands.append(f'lcp {name} /{name}')
                live[name]=size
        result=run(wd,commands)
        if result.returncode!=0:
            check=run(wd,['use -i churn.img /dev/a','partscan /dev/a','fsck.ext2 /dev/a1'])
            raise AssertionError((batch,commands,result.stdout,result.stderr,'fsck:',check.stdout,check.stderr))
        verify(wd)
    print('PASS 24 deterministic create/delete operations across 3 batches, read-only fsck after each')

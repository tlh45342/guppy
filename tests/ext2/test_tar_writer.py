"""Exercise the production TAR writer and verify output independently with Python tarfile."""
from pathlib import Path
import hashlib,subprocess,tarfile,tempfile
ROOT=Path(__file__).resolve().parents[2]
DRIVER=r'''
#include "tar_write.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){
 FILE *out,*in;long n;
 if(argc!=4)return 2;
 out=fopen(argv[1],"wb");if(!out)return 3;
 in=fopen(argv[2],"rb");if(!in)return 4;
 if(fseek(in,0,SEEK_END)|| (n=ftell(in))<0 || fseek(in,0,SEEK_SET))return 5;
 if(gu_tar_write_dir(out,"usr",0755)||gu_tar_write_dir(out,"usr/bin",0755)||
    gu_tar_write_file(out,"usr/bin/hello",in,(unsigned long)n,0755))return 6;
 fclose(in);in=fopen(argv[3],"rb");if(!in)return 7;
 if(gu_tar_write_file(out,"empty",in,0,0644)||gu_tar_write_end(out))return 8;
 fclose(in);return fclose(out)?9:0;
}
'''
with tempfile.TemporaryDirectory(prefix='tar-writer-',dir=ROOT/'tests'/'ext2') as t:
 wd=Path(t)
 (wd/'driver.c').write_text(DRIVER)
 p=subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),
    str(wd/'driver.c'),str(ROOT/'src'/'tar_write.c'),'-o',str(wd/'driver.exe')],capture_output=True,text=True)
 assert p.returncode==0,(p.stdout,p.stderr)
 content=bytes(range(256))*13
 (wd/'hello').write_bytes(content)
 (wd/'empty').write_bytes(b'')
 p=subprocess.run([str(wd/'driver.exe'),str(wd/'out.tar'),str(wd/'hello'),str(wd/'empty')],capture_output=True,text=True)
 assert p.returncode==0,(p.stdout,p.stderr)
 with tarfile.open(wd/'out.tar','r:') as tf:
    assert tf.getnames()==['usr','usr/bin','usr/bin/hello','empty'],tf.getnames()
    assert tf.getmember('usr/bin/hello').mode==0o755
    assert tf.extractfile('usr/bin/hello').read()==content
    assert tf.extractfile('empty').read()==b''
 assert (wd/'out.tar').stat().st_size%512==0
 print('PASS USTAR writer: directories, modes, 3328-byte file, empty file, independent tarfile read')

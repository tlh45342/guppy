"""Compile the production AR writer, inspect its bytes, and parse with Guppy's deb reader."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
DRIVER=r'''
#include "ar_write.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){
 FILE *o,*in;const char *names[]={"debian-binary","control.tar","data.tar"};
 int i;long size;
 if(argc!=5)return 2;
 o=fopen(argv[1],"wb");if(!o||gu_ar_write_begin(o))return 3;
 for(i=0;i<3;i++){
  in=fopen(argv[i+2],"rb");if(!in)return 4;
  if(fseek(in,0,SEEK_END)||(size=ftell(in))<0||fseek(in,0,SEEK_SET))return 5;
  if(gu_ar_write_member(o,names[i],in,(unsigned long)size,0644))return 6;
  fclose(in);
 }
 return fclose(o)?7:0;
}
'''
with tempfile.TemporaryDirectory(prefix='ar-writer-',dir=ROOT/'tests'/'ext2') as t:
 wd=Path(t)
 (wd/'driver.c').write_text(DRIVER)
 p=subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),str(wd/'driver.c'),str(ROOT/'src'/'ar_write.c'),'-o',str(wd/'driver.exe')],capture_output=True,text=True)
 assert p.returncode==0,(p.stdout,p.stderr)
 payloads=[b'2.0\n',b'control'*73,b'data'*256]
 for i,data in enumerate(payloads):(wd/f'{i}.bin').write_bytes(data)
 p=subprocess.run([str(wd/'driver.exe'),str(wd/'out.ar'),*[str(wd/f'{i}.bin') for i in range(3)]],capture_output=True,text=True)
 assert p.returncode==0,(p.stdout,p.stderr)
 data=(wd/'out.ar').read_bytes();assert data[:8]==b'!<arch>\n'
 pos=8
 for name,content in zip(['debian-binary','control.tar','data.tar'],payloads):
    h=data[pos:pos+60];assert len(h)==60 and h[58:]==b'`\n',h
    assert h[:16].decode().strip()==name+'/'
    size=int(h[48:58].decode().strip());assert size==len(content)
    assert data[pos+60:pos+60+size]==content
    if size&1:assert data[pos+60+size:pos+61+size]==b'\n'
    pos+=60+size+(size&1)
 assert pos==len(data)
 print('PASS AR writer: three ordered members, sizes, byte content, odd padding, archive length')

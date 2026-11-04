"""Compile the actual bounded TF diagnostic with an SPI character-device adapter."""
from pathlib import Path
import subprocess
import tempfile

repo=Path(__file__).resolve().parents[1]
source=(repo/'app/velaride/velaride_shell.c').read_text()
start=source.index('static int tf_spi_probe(void)')
body=source.index('{',start);depth=1;end=body+1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}');end+=1
function=source[start:end]
header=r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "velaride_ride_session.h"
#include "velaride_memo.h"
#define MSDOS_SUPER_MAGIC 0x4d44
#define SPIDEV_MMCSD(n) (n)
#define SPIDEV_MODE0 0
#define SPIIOC_TRANSFER 123
struct statfs {int f_type;};
struct spi_trans_s {bool deselect;size_t nwords;const void *txbuffer;void *rxbuffer;};
struct spi_sequence_s {uint32_t dev;uint8_t mode,nbits,ntrans;uint32_t frequency;struct spi_trans_s *trans;};
static bool riding,mounted,recording,fail_open,fail_ioctl,respond=true;
static int opens,closes,transfers;
void velaride_ride_session_get(struct velaride_ride_snapshot_s *s){memset(s,0,sizeof(*s));s->active=riding;}
void velaride_memo_get(struct velaride_memo_s *s){memset(s,0,sizeof(*s));s->state=recording?VELARIDE_MEMO_RECORDING:VELARIDE_MEMO_IDLE;}
static int statfs(const char *path,struct statfs *s){assert(strcmp(path,"/data/tf")==0);s->f_type=mounted?MSDOS_SUPER_MAGIC:0;return 0;}
static int fake_open(const char *path,int flags){(void)flags;assert(strcmp(path,"/dev/spi1")==0);opens++;errno=EIO;return fail_open?-1:4;}
static int close(int fd){assert(fd==4);closes++;return 0;}
static int ioctl(int fd,int command,unsigned long arg){
 assert(fd==4&&command==SPIIOC_TRANSFER);transfers++;
 struct spi_sequence_s *s=(void *)arg;
 assert(s->frequency==400000&&s->nbits==8&&s->mode==0&&s->ntrans==1);
 assert(s->trans->nwords==32&&s->trans->deselect);
 const unsigned char *tx=s->trans->txbuffer;
 for(int i=0;i<8;i++)assert(tx[i]==255);
 assert(tx[8]==0x40&&tx[9]==0&&tx[10]==0&&tx[11]==0&&tx[12]==0&&tx[13]==0x95);
 if(respond){memset(s->trans->rxbuffer,255,32);((unsigned char *)s->trans->rxbuffer)[17]=1;}
 errno=EIO;return fail_ioctl?-1:0;
}
#define open fake_open
'''
main=r'''
int main(void){
 riding=true;assert(tf_spi_probe()==EXIT_FAILURE&&opens==0);riding=false;
 recording=true;assert(tf_spi_probe()==EXIT_FAILURE&&opens==0);recording=false;
 mounted=true;assert(tf_spi_probe()==EXIT_FAILURE&&opens==0);mounted=false;
 assert(tf_spi_probe()==EXIT_SUCCESS&&opens==1&&closes==1&&transfers==1);
 respond=false;assert(tf_spi_probe()==EXIT_FAILURE&&closes==2);
 fail_ioctl=true;assert(tf_spi_probe()==EXIT_FAILURE&&closes==3);
 fail_open=true;assert(tf_spi_probe()==EXIT_FAILURE&&closes==3);
 puts("PASS: actual TF probe active/mounted guards, bounded CMD0-only packet, SPI profile, response/silent/error handling and descriptor cleanup");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='velaride-tf-probe-') as folder:
    path=Path(folder);(path/'test.c').write_text(header+function+main)
    subprocess.run(['cc','-std=c11','-D_GNU_SOURCE','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-I'+str(repo/'app/velaride'),str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)

"""Exercise the actual patched driver functions with a fault-injecting HAL."""
from pathlib import Path
import os
import subprocess
import tempfile

stage = Path(os.environ.get('VELARIDE_STAGE', '/home/wyj/velaride_product_features'))
source = (stage / 'vendor/sifli/chips/sf32lb52/sifli_spi.c').read_text()

def function(name):
    start = source.index('static ', source.index(' * Private Functions'))
    while True:
        at = source.index(name + '(', start)
        brace = source.index('{', at)
        if ';' not in source[at:brace]:
            start = source.rfind('\nstatic ', 0, at) + 1
            break
        start = at + len(name)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define SPI_POLL_TIMEOUT 100
#define SPI_WORD_TIMEOUT 20
#define SPI_IT_RXNE 1
#define SPI_IT_TXE 2
#define SPI_IT_ERR 4
#define HAL_OK 0
typedef int HAL_StatusTypeDef;
struct spi_dev_s { int unused; };
typedef struct { bool enabled, selected; unsigned irqs; int width; } SPI_HandleTypeDef;
struct sifli_spi_dev_s { struct spi_dev_s spidev; SPI_HandleTypeDef handle; bool failed; int nbits; unsigned port; };
static unsigned calls, words, aborted, fail_at;
static bool receive_only;
#define __HAL_SPI_ENABLE(h) ((h)->enabled=true)
#define __HAL_SPI_DISABLE(h) ((h)->enabled=false)
#define __HAL_SPI_RELEASE_CS(h) ((h)->selected=false)
#define __HAL_SPI_TAKE_CS(h) ((h)->selected=true)
#define __HAL_SPI_DISABLE_IT(h,bits) ((h)->irqs &= ~(bits))
#define spierr(...) ((void)0)
static void HAL_SPI_Abort(SPI_HandleTypeDef *h) { assert(!h->enabled && !h->selected && !h->irqs); aborted++; }
static void sf32lb_spi_select(struct spi_dev_s *dev, uint32_t id, bool selected) { (void)dev; (void)id; (void)selected; }
static int HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h, uint8_t *tx, uint8_t *rx, uint16_t count, unsigned timeout)
{
    assert(h->enabled); assert(timeout==SPI_WORD_TIMEOUT || timeout==SPI_POLL_TIMEOUT);
    calls++; if (calls==fail_at) return 1;
    assert(count==1); words+=count;
    for (unsigned i=0; i<count*(unsigned)h->width; i++) {
        if (receive_only) assert(tx[i]==0xff);
        rx[i]=tx[i]^0x5a;
    }
    return HAL_OK;
}
'''
for name in ('sifli_spi_select', 'sifli_spi_poll_failed', 'sifli_spi_send', 'sifli_spi_poll_exchange'):
    code += '\n' + function(name) + '\n'
code += r'''
static struct sifli_spi_dev_s device(int bits)
{
    calls=words=aborted=fail_at=0; receive_only=false;
    struct sifli_spi_dev_s d={0}; d.nbits=bits; d.handle.width=bits>8?2:1;
    d.handle.selected=true; return d;
}
int main(void)
{
    struct sifli_spi_dev_s d=device(8);
    assert(sifli_spi_send(&d.spidev,0xa7)==(0xa7^0x5a));
    d=device(16); assert(sifli_spi_send(&d.spidev,0xabcd)==(0xabcd^0x5a5a));
    fail_at=2; d.handle.irqs=7;
    assert(sifli_spi_send(&d.spidev,0)==UINT16_MAX);
    assert(d.failed && aborted==1 && !d.handle.selected && !d.handle.enabled);
    assert(sifli_spi_send(&d.spidev,0)==UINT16_MAX && calls==2);
    sifli_spi_select(&d.spidev,0,false); assert(!d.failed);
    sifli_spi_select(&d.spidev,0,true); assert(d.handle.selected && d.handle.enabled);
    assert(sifli_spi_send(&d.spidev,0)==0x5a5a);

    for(int bits=8;bits<=16;bits+=8) {
        d=device(bits); size_t count=65537, bytes=count*d.handle.width;
        uint8_t *rx=malloc(bytes+2),*tx=malloc(bytes); assert(rx&&tx);
        memset(rx,0x91,bytes+2); memset(tx,0x37,bytes);
        receive_only=true; sifli_spi_poll_exchange(&d,NULL,rx+1,count);
        assert(words==count && calls==count && rx[0]==0x91 && rx[bytes+1]==0x91);
        for(size_t i=1;i<=bytes;i++) assert(rx[i]==(0xff^0x5a));
        receive_only=false; sifli_spi_poll_exchange(&d,tx,NULL,count);
        sifli_spi_poll_exchange(&d,tx,tx,count);
        for(size_t i=0;i<bytes;i++) assert(tx[i]==(0x37^0x5a));
        free(tx);free(rx);
    }
    d=device(8); uint8_t rx[150]; receive_only=true;fail_at=2;d.handle.irqs=7;
    sifli_spi_poll_exchange(&d,NULL,rx,sizeof(rx));
    assert(d.failed && calls==2 && words==1 && aborted==1);
    for(size_t i=1;i<sizeof(rx);i++) assert(rx[i]==0xff);
    sifli_spi_poll_exchange(&d,NULL,rx,10); assert(calls==2);
    d=device(8);sifli_spi_poll_exchange(&d,NULL,NULL,0);assert(!calls);
    puts("PASS: actual SPI 8/16-bit idle clocks, RX/TX/in-place, 65537 words without truncation, single-word HAL limit, bounds, timeout cleanup, fail latch and de-select recovery");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(path/'test.c'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)

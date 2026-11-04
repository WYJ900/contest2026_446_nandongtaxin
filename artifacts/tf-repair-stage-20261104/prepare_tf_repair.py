"""Preserve the actual stage, restore the accepted app, apply TF-only changes."""
from pathlib import Path
import difflib
import hashlib
import json
import tarfile

root = Path(__file__).resolve().parents[1]
repo = root / 'my-contest-repo'
stage = Path('//wsl.localhost/Ubuntu-22.04/home/wyj/velaride_product_features')
save = root / 'backup/tf-repair-20261104'
save.mkdir(parents=True, exist_ok=True)
paths = ['vendor/sifli/chips/sf32lb52/sifli_spi.c',
         'nuttx/drivers/mmcsd/mmcsd_spi.c']
for name in paths:
    before = save / 'before' / name
    before.parent.mkdir(parents=True, exist_ok=True)
    if not before.exists():
        before.write_bytes((stage / name).read_bytes())
app = stage / 'apps/examples/velaride'
if not (save / 'before-app.tar.gz').exists():
    with tarfile.open(save / 'before-app.tar.gz', 'w:gz') as archive:
        archive.add(app, arcname='velaride')
with tarfile.open(repo / 'artifacts/install-stage-20261104/app-source.tar.gz') as archive:
    for member in archive.getmembers():
        if member.isfile():
            target = app.parent / member.name
            if not target.resolve().is_relative_to(app.resolve()):
                raise ValueError(member.name)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.extractfile(member).read())

def replace_once(text, old, new):
    assert text.count(old) == 1, old[:100]
    return text.replace(old, new, 1)

old = (save / 'before' / paths[0]).read_text()
new = replace_once(old, '#include <stdbool.h>', '#include <stdbool.h>\n#include <string.h>')
new = replace_once(new, '#define SPI_XFER_TIMEOUT    5000', '#define SPI_POLL_TIMEOUT    100\n#define SPI_WORD_TIMEOUT    20\n#define SPI_XFER_TIMEOUT    5000')
new = replace_once(new, '    int      nbits;', '    bool     failed;                /* Stop clocks until de-select after error */\n    int      nbits;')
new = replace_once(new, '        __HAL_SPI_RELEASE_CS(&priv->handle);\n        __HAL_SPI_DISABLE(&priv->handle);', '        __HAL_SPI_RELEASE_CS(&priv->handle);\n        __HAL_SPI_DISABLE(&priv->handle);\n        priv->failed = false;')
start = new.index('static uint32_t sifli_spi_send(struct spi_dev_s *dev, uint32_t wd)\n{')
end = new.index('\n/****************************************************************************', start)
new = new[:start] + '''/* Error cleanup runs in the calling thread, never from an ISR. */
static void sifli_spi_poll_failed(struct sifli_spi_dev_s *priv)
{
    __HAL_SPI_DISABLE_IT(&priv->handle, SPI_IT_RXNE | SPI_IT_TXE | SPI_IT_ERR);
    __HAL_SPI_RELEASE_CS(&priv->handle);
    __HAL_SPI_DISABLE(&priv->handle);
    HAL_SPI_Abort(&priv->handle);
    priv->failed = true;
    spierr("SPI%u: polling failed; CS released, IRQs disabled\\n",
           priv->port + 1);
}

static uint32_t sifli_spi_send(struct spi_dev_s *dev, uint32_t wd)
{
    struct sifli_spi_dev_s *priv = (struct sifli_spi_dev_s *)dev;
    uint16_t tx = (uint16_t)wd;
    uint16_t rx = UINT16_MAX;
    HAL_StatusTypeDef status;

    if (priv->failed)
    {
        return priv->nbits <= 8 ? UINT8_MAX : UINT16_MAX;
    }

    __HAL_SPI_ENABLE(&priv->handle);
    status = HAL_SPI_TransmitReceive(&priv->handle, (uint8_t *)&tx,
                                    (uint8_t *)&rx, 1, SPI_WORD_TIMEOUT);
    if (status != HAL_OK)
    {
        sifli_spi_poll_failed(priv);
        return priv->nbits <= 8 ? UINT8_MAX : UINT16_MAX;
    }

    return priv->nbits <= 8 ? rx & UINT8_MAX : rx;
}

/* TF uses non-DMA SPI. Synchronous chunks keep stack buffers owned until
 * completion, and supply idle ones for receive-only transfers. The HAL's
 * Receive_IT otherwise transmits the uninitialized receive buffer itself.
 */
static void sifli_spi_poll_exchange(struct sifli_spi_dev_s *priv,
                                   const void *txbuffer, void *rxbuffer,
                                   size_t nwords)
{
    uint16_t dummy[64];
    uint16_t discard[64];
    size_t width = priv->nbits <= 8 ? 1 : 2;
    const uint8_t *tx = txbuffer;
    uint8_t *rx = rxbuffer;

    memset(dummy, 0xff, sizeof(dummy));
    if (!tx && rx)
    {
        memset(rx, 0xff, nwords * width);
    }

    while (nwords > 0 && !priv->failed)
    {
        uint16_t count = nwords > 64 ? 64 : (uint16_t)nwords;
        __HAL_SPI_ENABLE(&priv->handle);
        HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
            &priv->handle, tx ? (uint8_t *)tx : (uint8_t *)dummy,
            rx ? rx : (uint8_t *)discard, count, SPI_POLL_TIMEOUT);
        if (status != HAL_OK)
        {
            sifli_spi_poll_failed(priv);
            break;
        }

        if (tx) tx += count * width;
        if (rx) rx += count * width;
        nwords -= count;
    }
}
''' + new[end:]
needle = '    /* Calculate number of bytes to transfer */'
new = replace_once(new, needle, '''    if (!priv->spi_dma_tx_flag && !priv->spi_dma_rx_flag)
    {
        sifli_spi_poll_exchange(priv, txbuffer, rxbuffer, nwords);
        return;
    }

''' + needle)
new = replace_once(new, '    priv->nbits      = 8;', '    priv->nbits      = 8;\n    priv->failed     = false;')
(stage / paths[0]).write_text(new, newline='\n')

# Bounded initialization trace: no sectors or user content are printed.
old_mmc = (save / 'before' / paths[1]).read_text()
mmc = replace_once(old_mmc, '#include <stdio.h>', '#include <stdio.h>\n#include <syslog.h>')
mmc = replace_once(mmc, '  uint8_t  state;', '  uint8_t trace_left;        /* Bounded card initialization diagnostics */\n  uint8_t  state;')
mmc = replace_once(mmc, '  slot->state |= MMCSD_SLOTSTATUS_NOTREADY;', '  slot->trace_left = 16;\n  syslog(LOG_ERR, "TF_INIT: begin\\n");\n  slot->state |= MMCSD_SLOTSTATUS_NOTREADY;')
mmc = replace_once(mmc, '      ferr("ERROR: Failed: i=%d response=%02x\\n", i, response);', '      if (slot->trace_left > 0)\n        {\n          slot->trace_left--;\n          syslog(LOG_ERR, "TF_INIT: CMD%u no response R1=%02x\\n",\n                 cmd->cmd & 0x3f, response);\n        }\n      ferr("ERROR: Failed: i=%d response=%02x\\n", i, response);')
mmc = replace_once(mmc, '  return result;\n}\n\n/****************************************************************************\n * Name: mmcsd_setblklen', '''  if (slot->trace_left > 0)
    {
      slot->trace_left--;
      syslog(LOG_ERR, "TF_INIT: CMD%u arg=%08lx R1=%02lx R7=%08lx OCR=%08lx\\n",
             cmd->cmd & 0x3f, (unsigned long)arg, (unsigned long)result,
             (unsigned long)slot->r7, (unsigned long)slot->ocr);
    }

  return result;
}

/****************************************************************************
 * Name: mmcsd_setblklen''')
(stage / paths[1]).write_text(mmc, newline='\n')
patch = ''.join(''.join(difflib.unified_diff(
    (save / 'before' / name).read_text().splitlines(True),
    (stage / name).read_text().splitlines(True),
    fromfile='a/' + name, tofile='b/' + name)) for name in paths)
(repo / 'patches/tf-spi-recovery.patch').write_text(patch, newline='\n')
manifest = {name: hashlib.sha256((save / 'before' / name).read_bytes()).hexdigest() for name in paths}
(save / 'before-driver-sha256.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('Accepted app restored in build stage; scoped SPI repair and bounded TF trace applied.')

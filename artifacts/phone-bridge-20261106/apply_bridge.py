"""After `patch -p3 < bridge-5019760.patch`: drop the unrelated TF SPI probe
that came with that commit, and use a page title whose glyphs exist in the
28 px title font of this build (it lacks 天/气)."""
import re
import sys

app = sys.argv[1]

p = f'{app}/velaride_shell.c'
s = open(p, encoding='utf-8').read()
s, n1 = re.subn(r'#if defined\(CONFIG_SPI_DRIVER\) && defined\(CONFIG_SPI_EXCHANGE\)\n#include <nuttx/spi/spi_transfer.h>\n#include <sys/statfs.h>\n#endif\n', '', s)
start = s.index('#if defined(CONFIG_SPI_DRIVER) && defined(CONFIG_SPI_EXCHANGE)\nstatic int tf_spi_probe(void)')
end = s.index('#endif\n', s.index('return ret==0&&r1==1?EXIT_SUCCESS:EXIT_FAILURE;', start)) + len('#endif\n\n')
s = s[:start] + s[end:]
s, n2 = re.subn(r'#if defined\(CONFIG_SPI_DRIVER\) && defined\(CONFIG_SPI_EXCHANGE\)\n  if\(argc==2&&strcmp\(argv\[1\],"tf-probe"\)==0\)return tf_spi_probe\(\);\n#endif\n', '', s)
assert n1 == 1 and n2 == 1 and 'tf_spi_probe' not in s
open(p, 'w', encoding='utf-8').write(s)

p = f'{app}/velaride_sport_ui.c'
s = open(p, encoding='utf-8').read()
assert s.count('"手机与天气"') == 2
s = s.replace('"手机与天气"', '"手机消息"')
open(p, 'w', encoding='utf-8').write(s)
print('bridge applied')

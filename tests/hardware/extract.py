"""Compile actual driver functions with deterministic host HAL adapters."""
from pathlib import Path
import sys

workspace = Path(sys.argv[1])
output = Path(sys.argv[2])
output.mkdir(parents=True, exist_ok=True)
here = Path(__file__).parent


def function(source, name, declaration='static int '):
    marker = declaration + name + '('
    start = source.index(marker)
    # Skip a forward declaration, if present.
    while source.find(';', start) < source.find('{', start):
        start = source.index(marker, start + len(marker))
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end]


for name, source_path, functions in [
    ('i2c', 'vendor/sifli/chips/sf32lb52/sifli_i2c.c',
     ['i2c_mem_ops', 'sifli_i2c_transfer']),
    ('adc', 'vendor/sifli/chips/sf32lb52/sf32lb_adc.c',
     ['adc_read', 'adc_read_work']),
]:
    code = (workspace / source_path).read_text(encoding='utf-8')
    template = (here / (name + '_test.in.c')).read_text(encoding='utf-8')
    generated = template.replace('/* ACTUAL_DRIVER_FUNCTIONS */',
                                 '\n\n'.join(function(code, f) for f in functions))
    (output / (name + '_test.c')).write_text(generated, encoding='utf-8')

for name in ['ltr303', 'mmc5603']:
    code = (workspace / f'nuttx/drivers/sensors/{name}.c').read_text(encoding='utf-8')
    code = '\n'.join(line for line in code.splitlines() if not line.startswith('#include'))
    template = (here / f'{name}_test.in.c').read_text(encoding='utf-8')
    (output / f'{name}_test.c').write_text(
        template.replace('/* ACTUAL_DRIVER_SOURCE */', code), encoding='utf-8')

code = (workspace / 'nuttx/drivers/timers/arch_timer.c').read_text(encoding='utf-8')
functions = [function(code, 'current_usec', 'static uint64_t '),
             function(code, 'up_timer_gettime', 'int weak_function '),
             function(code, 'up_timer_gettick', 'int weak_function ')]
template = (here / 'arch_timer_test.in.c').read_text(encoding='utf-8')
(output / 'arch_timer_test.c').write_text(
    template.replace('/* ACTUAL_DRIVER_FUNCTIONS */', '\n\n'.join(functions)),
    encoding='utf-8')

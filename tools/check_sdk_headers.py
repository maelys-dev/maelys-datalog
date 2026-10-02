#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Check the installed SDK's audience boundaries, including negative consumers."""
import argparse
import itertools
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

APPLICATION = (
    'datalog.h', 'datalog_builders.h', 'datalog_window.h', 'datalog_inputs.h',
    'datalog_resources.h', 'datalog_program.h', 'datalog_explanations.h',
)
INTEGRATOR = ('datalog_extension.h',)
AUTHOR = ('datalog_module.h', 'datalog_frontend.h', 'datalog_backend.h')
DESCRIPTORS = ('backend', 'backend_v6', 'backend_v7', 'frontend',
               'filter_module', 'planner_module')


def check(prefix):
    headers = prefix / 'include' / 'maelys'
    levels = {name: level for level, names in enumerate(
        (APPLICATION, INTEGRATOR, AUTHOR)) for name in names}
    assert {p.name for p in headers.glob('*.h')} == set(levels), 'header inventory'
    graph = {}
    for name, level in levels.items():
        source = re.sub(r'/\*.*?\*/', '', (headers / name).read_text(), flags=re.S)
        deps = re.findall(r'^\s*#\s*include\s*[<"](?:maelys/)?(datalog[^>"/]*\.h)[>"]',
                          source, re.M)
        graph[name] = deps
        for dep in deps:
            assert dep in levels and levels[dep] <= level, f'{name} includes upward: {dep}'
    # The facade/builders guarded pair is deliberate; no other include cycle.
    def visit(name, stack):
        if name in stack:
            assert set(stack[stack.index(name):]) == {'datalog.h', 'datalog_builders.h'}, stack
            return
        for dep in graph[name]:
            visit(dep, stack + [name])
    for name in graph:
        visit(name, [])
    with tempfile.TemporaryDirectory(prefix='maelys-header-audience-') as temp:
        source = Path(temp) / 'consumer.c'
        app = ''.join(f'#include <maelys/{name}>\n' for name in APPLICATION)
        integration = app + '#include <maelys/datalog_extension.h>\n'
        for language, env, default, standard in (
            ('c', 'CC', 'cc', 'c11'), ('c++', 'CXX', 'c++', 'c++17')):
            command = shlex.split(os.environ.get(env, default)) + [
                '-x', language, '-std=' + standard, '-Wall', '-Wextra', '-Werror',
                '-pedantic-errors', '-I' + str(prefix / 'include'), '-fsyntax-only', str(source)]
            def compile_probe(text, success, label):
                source.write_text(text + '\n')
                result = subprocess.run(command, capture_output=True, text=True)
                assert (result.returncode == 0) == success, label + '\n' + result.stderr
            compile_probe(app + 'int main(void) { return 0; }', True, 'application')
            compile_probe(app + 'int main(void) { maelys_datalog_caller_allocator_t a = MAELYS_DATALOG_CALLER_ALLOCATOR_INIT; maelys_datalog_session_allocation_stats_t s = MAELYS_DATALOG_ALLOCATION_STATS_INIT; (void)&maelys_datalog_session_get_allocation_stats; return (int)(a.reserved+s.reserved); }', True, 'application allocator and telemetry')
            for private_type in ('allocation_service', 'allocation_budget', 'session_allocation_resources'):
                compile_probe(app + f'maelys_datalog_{private_type}_t *service;', False, 'application allocation service leak')
            for descriptor in DESCRIPTORS:
                typename = f'maelys_datalog_{descriptor}_t'
                compile_probe(app + f'{typename} *provider;', False, 'application descriptor leak')
                compile_probe(integration + f'{typename} *provider;', True, 'integrator pointer')
                compile_probe(integration + f'int main(void) {{ return sizeof({typename}); }}',
                              False, 'integrator descriptor definition leak')
            for symbol in ('backend_emit', 'backend_charge', 'backend_reference',
                           'session_config_set_backend_v6', 'program_add_rule',
                           'domain_builder_add'):
                compile_probe(app + f'int main(void) {{ (void)&maelys_datalog_{symbol}; return 0; }}',
                              False, 'application provider service leak')
            # Every ordering of the three author headers must accept the forward
            # types, named definitions and C/C++ linkage without redeclarations.
            for order in itertools.permutations(AUTHOR):
                complete = integration + ''.join(f'#include <maelys/{name}>\n' for name in order)
                complete += 'int main(void) { return ' + ' + '.join(
                    f'(int)sizeof(maelys_datalog_{d}_t)' for d in DESCRIPTORS) + '; }'
                compile_probe(complete, True, 'author include order')
    print('SDK headers: 11 files; audience graph and C11/C++17 positive/negative consumers PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path, help='clean installed SDK prefix')
    check(parser.parse_args().prefix.resolve())

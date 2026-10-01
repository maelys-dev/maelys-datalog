#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare public declarations and native layouts across a header-only migration.

The scanner targets this SDK's declaration grammar, not arbitrary C. It fails on
unconsumed input. Clang independently compiles the headers and enumerates public
records, including callback field types; layout probes execute on the host ABI.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess
import tempfile

TAGS = tuple('maelys_datalog_' + name for name in (
    'backend', 'backend_v6', 'backend_v7', 'frontend', 'filter_module', 'planner_module'))


def inventory(directory):
    declarations, macros = [], Counter()
    for header in sorted(directory.glob('*.h')):
        source = re.sub(r'/\*.*?\*/', '', header.read_text(), flags=re.S)
        source = re.sub(r'//[^\n]*', '', source).replace('\\\n', '')
        for match in re.finditer(r'^\s*#define\s+(\w+)([^\n]*)', source, re.M):
            if not match[1].endswith('_H'):
                # Preserve every conditional C/C++ definition, not only the last.
                macros[(match[1], re.sub(r'\s+', '', match[2]))] += 1
        if header.name == 'datalog_builders.h':
            continue  # Compared byte-for-byte, including its inline bodies.
        source = source.replace('#ifdef __cplusplus\n}\n#endif', '')
        source = re.sub(r'^\s*#.*$', '', source, flags=re.M).replace('extern "C" {', '')
        for tag in TAGS:
            source = source.replace('typedef struct ' + tag + '_t {', 'typedef struct {')
            source = source.replace('typedef struct ' + tag + '_t ' + tag + '_t;', '')
        start, depth = 0, 0
        for index, char in enumerate(source):
            if char == '{':
                depth += 1
            if char == '}':
                depth -= 1
            inline_end = char == '}' and source[start:index].strip().startswith('static inline')
            if depth == 0 and (char == ';' or inline_end):
                declaration = re.sub(r'\s+', '', source[start:index + 1])
                start = index + 1
                if declaration:
                    declarations.append(declaration)
        assert not source[start:].strip() and depth == 0, (header, source[start:])
    return Counter(declarations), macros


def records(ast):
    nodes = {}

    def walk(node):
        if 'id' in node:
            nodes[node['id']] = node
        for child in node.get('inner', []):
            walk(child)

    def record(node):
        if node.get('kind') in ('PointerType', 'FunctionProtoType'):
            return None  # An opaque pointer/callback is not a record typedef.
        if node.get('kind') == 'RecordType':
            return nodes.get(node.get('decl', {}).get('id'))
        for child in node.get('inner', []):
            result = record(child)
            if result:
                return result
        return None

    walk(ast)
    result = {}
    for node in ast['inner']:
        if node['kind'] != 'TypedefDecl' or not node.get('name', '').startswith('maelys_datalog_'):
            continue
        definition = record(node)
        if definition and definition.get('completeDefinition'):
            result[node['name']] = [field['name'] for field in definition.get('inner', [])
                                    if field['kind'] == 'FieldDecl']
    return result


def layout(prefix, scratch, label):
    include = prefix / 'include'
    source = ''.join('#include <maelys/' + h.name + '>\n'
                     for h in sorted((include / 'maelys').glob('*.h')))
    unit = scratch / (label + '.c')
    unit.write_text(source)
    command = ['clang', '-std=c11', '-pedantic-errors', '-Wall', '-Wextra', '-Werror',
               '-I' + str(include)]
    ast = json.loads(subprocess.check_output(command + [
        '-Xclang', '-ast-dump=json', '-fsyntax-only', str(unit)]))
    public = records(ast)
    source += '#include <stdio.h>\n#include <stddef.h>\nint main(void) {\n'
    for name, fields in sorted(public.items()):
        source += f'printf("{name} %zu %zu", sizeof({name}), _Alignof({name}));\n'
        for field in fields:
            source += f'printf(" {field}=%zu", offsetof({name}, {field}));\n'
        source += 'puts("");\n'
    source += 'return 0; }\n'
    unit.write_text(source)
    executable = scratch / label
    subprocess.run(command + [str(unit), '-o', str(executable)], check=True)
    cpp = scratch / (label + '.cpp')
    cpp.write_text(''.join('#include <maelys/' + h.name + '>\n'
                           for h in sorted((include / 'maelys').glob('*.h'))) +
                   ''.join(f'void descriptor_{i}({tag}_t *) {{}}\n'
                           for i, tag in enumerate(TAGS)))
    obj = scratch / (label + '.o')
    subprocess.run(['clang++', '-std=c++17', '-pedantic-errors', '-Wall', '-Wextra',
                    '-Werror', '-I' + str(include), '-c', str(cpp), '-o', str(obj)], check=True)
    symbols = subprocess.check_output(['nm', '-g', str(obj)], text=True)
    names = sorted(line.split()[-1] for line in symbols.splitlines() if 'descriptor_' in line)
    assert len(names) == len(TAGS), 'C++ descriptor linkage probe incomplete'
    return public, subprocess.check_output([str(executable)]), names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path, help='baseline SDK prefix')
    parser.add_argument('after', type=Path, help='candidate SDK prefix')
    args = parser.parse_args()
    before, after = args.before.resolve(), args.after.resolve()
    old, old_macros = inventory(before / 'include/maelys')
    new, new_macros = inventory(after / 'include/maelys')
    assert old == new, {'missing': list((old - new).elements()),
                        'added': list((new - old).elements())}
    assert old_macros == new_macros, 'public macros changed'
    assert (before / 'include/maelys/datalog_builders.h').read_bytes() == (
        after / 'include/maelys/datalog_builders.h').read_bytes(), 'builders changed'
    with tempfile.TemporaryDirectory(prefix='maelys-header-relocation-') as temp:
        old_records, old_layout, old_cpp = layout(before, Path(temp), 'before')
        new_records, new_layout, new_cpp = layout(after, Path(temp), 'after')
        assert old_records == new_records, 'record fields changed'
        assert old_layout == new_layout, 'native record size/alignment/offset changed'
        assert old_cpp == new_cpp, 'C++ descriptor type linkage changed'
    print(f'{sum(old.values())} normalized declarations, {len({name for name, _ in old_macros})} macro names, '
          f'{len(old_records)} native record layouts and six C++ type names identical')


if __name__ == '__main__':
    main()

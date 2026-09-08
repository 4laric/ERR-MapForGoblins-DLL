"""Create an AP map preset from a supplied INI, preserving unrelated settings."""
import argparse
import re
from pathlib import Path

SETTINGS = {
    'Loot': {'show_material_nodes': 'false', 'show_crafting_materials': 'true'},
    'Archipelago': {'ap_checks_only': 'true', 'ap_progression_only': 'false',
                    'ap_in_logic_only': 'true', 'ap_progression_aura': 'true',
                    'ap_hint_aura': 'true'},
}

def apply_preset(text):
    newline = '\r\n' if '\r\n' in text else '\n'
    pending = {section: dict(values) for section, values in SETTINGS.items()}
    output, section, seen_sections, seen_keys = [], None, set(), set()
    def flush():
        if section in pending:
            output.extend(f'{key} = {value}' for key, value in pending[section].items())
            pending[section].clear()
    for line in text.splitlines():
        header = re.fullmatch(r'\s*\[([^]]+)\]\s*(?:[;#].*)?', line)
        if header:
            flush()
            section = header[1]
            if section in SETTINGS and section in seen_sections:
                raise ValueError('Duplicate preset section: ' + section)
            seen_sections.add(section)
        setting = re.match(r'(\s*)([A-Za-z0-9_]+)(\s*=\s*)(.*)', line)
        if section in SETTINGS and setting and setting[2] in SETTINGS[section]:
            key = setting[2]
            if (section, key) in seen_keys:
                raise ValueError('Duplicate preset setting: ' + key)
            seen_keys.add((section, key))
            # Keep comments; replace only this setting's value.
            comment = re.search(r'\s*[;#].*$', setting[4])
            line = setting[1] + key + setting[3] + SETTINGS[section][key] + (comment[0] if comment else '')
            pending[section].pop(key, None)
        output.append(line)
    flush()
    for name, values in pending.items():
        if values:
            output.extend(['', f'[{name}]'])
            output.extend(f'{key} = {value}' for key, value in values.items())
    return newline.join(output) + newline

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error('Use a separate output path so your existing settings remain available')
    with args.input.open(encoding='utf-8-sig', newline='') as handle:
        result = apply_preset(handle.read())
    with args.output.open('w', encoding='utf-8', newline='') as handle:
        handle.write(result)

if __name__ == '__main__':
    main()

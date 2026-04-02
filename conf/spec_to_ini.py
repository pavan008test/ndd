#This script converts a JSON specification of configuration parameters (.spec) into an INI file format.
# It also validates the parameters against defined rules such as type, allowed values, and ranges. 
# If validation fails, it reports errors and exits with a non-zero status.
# Usage: python spec_to_ini.py <spec.json> [output.ini]
import json
import sys
from pathlib import Path

def format_value(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return str(v)
    # string
    s = str(v)
    # wrap in quotes only if contains spaces or starts/ends with quote already
    if any(c.isspace() for c in s) or s.startswith('"') or s.endswith('"'):
        # ensure we have exactly one pair of quotes
        if not (s.startswith('"') and s.endswith('"')):
            s = f'"{s}"'
    return s

# --- Validation helpers ---
_TYPE_MAP = {
    'int': int,
    'float': float,
    'bool': bool,
    'string': str,
}

def _coerce_value(type_name, value):
    py_type = _TYPE_MAP.get(type_name)
    if py_type is None:
        raise ValueError(f"Unknown type '{type_name}'")
    # If value already proper type keep; else attempt conversion (except bool which should not be coerced from int automatically)
    if isinstance(value, py_type):
        return value
    if py_type is bool:
        if isinstance(value, str):
            if value.lower() in ('true','false'): return value.lower() == 'true'
        raise ValueError(f"Expected bool got {value!r}")
    try:
        return py_type(value)
    except Exception as e:
        raise ValueError(f"Failed to coerce {value!r} to {type_name}: {e}")

def has_effective_rule(rules_dict):
    if not rules_dict:
        return False
    if 'allowed_values' in rules_dict and isinstance(rules_dict.get('allowed_values'), list) and rules_dict['allowed_values']:
        return True
    for k in ('min','max','max_length'):
        if k in rules_dict:
            return True
    other_keys = set(rules_dict.keys()) - {'allowed_values','min','max','max_length'}
    if other_keys:
        return True
    return False

def validate_param(section, key, meta):
    errors = []
    if not isinstance(meta, dict):
        errors.append(f"{section}.{key}: meta not dict")
        return errors
    type_name = meta.get('type')
    value = meta.get('value')
    rules = meta.get('rules', {}) or {}
    if type_name not in _TYPE_MAP:
        errors.append(f"{section}.{key}: unknown type {type_name}")
        return errors
    if not has_effective_rule(rules):
        errors.append(f"{section}.{key}: must define at least one validation rule (min/max/max_length or non-empty allowed_values)")
        return errors
    # Type check / coerce
    try:
        coerced = _coerce_value(type_name, value)
    except ValueError as e:
        errors.append(f"{section}.{key}: {e}")
        return errors
    # Validate rules
    # allowed_values
    allowed = rules.get('allowed_values')
    if isinstance(allowed, list) and allowed:
        if coerced not in allowed:
            errors.append(f"{section}.{key}: value {coerced!r} not in allowed_values {allowed}")
    # min / max numeric
    if isinstance(coerced, (int,float)):
        if 'min' in rules and coerced < rules['min']:
            errors.append(f"{section}.{key}: value {coerced} < min {rules['min']}")
        if 'max' in rules and coerced > rules['max']:
            errors.append(f"{section}.{key}: value {coerced} > max {rules['max']}")
    # string rules
    if isinstance(coerced, str):
        if 'max_length' in rules and len(coerced) > rules['max_length']:
            errors.append(f"{section}.{key}: length {len(coerced)} > max_length {rules['max_length']}")
        allowed_strs = rules.get('allowed_values')
        if isinstance(allowed_strs, list) and allowed_strs:
            if coerced not in allowed_strs:
                errors.append(f"{section}.{key}: value {coerced!r} not in allowed_values {allowed_strs}")
    return errors

def validate_spec(spec: dict) -> list:
    all_errors = []
    for section, params in spec.items():
        if not isinstance(params, dict):
            # section value directly; skip detailed validation
            continue
        for key, meta in params.items():
            all_errors.extend(validate_param(section, key, meta))
    return all_errors

def spec_to_ini(spec: dict) -> str:
    lines = []
    for section, params in spec.items():
        lines.append(f'[{section}]')
        if not isinstance(params, dict):
            # section value directly (rare)
            lines.append(f'{section} = {format_value(params)}')
            lines.append("")
            continue
        for key, meta in params.items():
            # meta expected dict with at least "value"
            if isinstance(meta, dict) and "value" in meta:
                val = meta["value"]
            else:
                # fallback treat meta itself as value
                val = meta
            lines.append(f'{key} = {format_value(val)}')
        lines.append("")  # blank line after each section
    return "\n".join(lines).rstrip() + "\n"

def main():
    if len(sys.argv) < 2:
        print("Usage: python spec_to_ini.py <spec.json> [output.ini]", file=sys.stderr)
        sys.exit(1)
    spec_path = Path(sys.argv[1])
    with spec_path.open() as f:
        spec = json.load(f)
    errors = validate_spec(spec)
    if errors:
        print("Validation failed (" + str(len(errors)) + " errors):", file=sys.stderr)
        for e in errors:
            print(" - " + e, file=sys.stderr)
        sys.exit(2)
    ini_text = spec_to_ini(spec)
    if len(sys.argv) > 2:
        out_path = Path(sys.argv[2])
        out_path.write_text(ini_text)
    else:
        sys.stdout.write(ini_text)

if __name__ == "__main__":
    main()


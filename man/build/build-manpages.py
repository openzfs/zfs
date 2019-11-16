#!/usr/bin/env -S python3 -I
import argparse
from collections import defaultdict
import os
import re
import sys

from jinja2 import Environment, FileSystemLoader

def defines_in_header_file(path):
    """Processes a C header file for lines beginning with #define,
       extracting the symbol name and value for each one. Strips
       double quotes from the value, if present.
       Ignores all other lines.
    """
    with open(path, mode='r', encoding='UTF-8') as config_file:
        for line in config_file:
            # Find all lines that begin with '#define' (no leading whitespace)
            # Use named groups to extract like:
            # #define <name> <value> // OPTIONAL COMMENT
            # whitespace-delimited name and value are kept; rest is discarded
            defines = re.fullmatch(
                r"""\#define\s+            # line must begin with #define
                                           # followed by one or more whitespace
                                           # characters; '#' in #define must be
                                           # escaped because re.VERBOSE is set

                    (?P<name>[^\s]+)\s+    # symbol name is second token
                                           # symbol value is third token

                    (?P<quot>['"]?)        # ... and may begin with an optional
                                           #     single or double quote

                    (?P<value>.*?)         # for the value, match any character
                                           # but non-greedily until ...

                    (?P=quot)\s*           # the character seen before the value,
                                           # zero or more whitespace characters,
                                           # or end of line

                    (?://.*)?              # optionally strip //-style comment
                                           # from the end of the line
                 """,
                line,
                flags=re.VERBOSE
            )
            if defines is not None:
                yield defines.group('name'), defines.group('value')


def render_template_file(path, context):
    """Creates a Jinja2 Environment object
       and renders the template file at the provided path,
       relative to the current working directory,
       with the provided dict as context
    """
    env = Environment(
        loader=FileSystemLoader(os.getcwd(), followlinks=True),
        trim_blocks=True,
        line_statement_prefix=r'.\" %'
    )

    template = env.get_template(path)

    return template.render(**context)


def generate_uname_from_header(header_defs):
    if header_defs['SYSTEM_LINUX'] is not None:
        return 'Linux'
    elif header_defs['SYSTEM_FREEBSD'] is not None:
        return 'FreeBSD'


def main():
    parser = argparse.ArgumentParser(
        description='OpenZFS manual page template processor'
    )
    parser.add_argument('input', help='template file to render')
    parser.add_argument(
        '-o', '--output',
        default='-',
        metavar='FILENAME',
        help='File to write output to or "-" for standard output'
    )
    parser.add_argument(
        '-c', '--zfs-config',
        required=True,
        help='`zfs_config.h` for the current build'
    )
    args = parser.parse_args()

    # Set output to stdout or provided filename
    if args.output == '-':
        output = sys.stdout
    else:
        output = open(args.output, mode='w', encoding='UTF-8')

    # Read all '#define' lines from provided zfs_config.h
    # Any symbols not defined will return None rather than
    # raise KeyError because header_defs is a defaultdict
    header_defs_generator = defines_in_header_file(args.zfs_config)
    context_vars = defaultdict(lambda: None)
    context_vars.update(header_defs_generator)

    # Additional values to inject into context
    additional_context = {
        'uname': generate_uname_from_header(context_vars),
    }
    context_vars.update(additional_context)

    output.write(
        render_template_file(args.input, context_vars)
    )
    output.close()


if __name__ == "__main__":
    main()

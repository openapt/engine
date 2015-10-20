#!/usr/bin/env python
# This file is part of Viper - https://github.com/viper-framework/viper
# See the file 'LICENSE' for copying permission.

import argparse

from viper.core.ui import console
from viper.core.project import __project__

parser = argparse.ArgumentParser()
parser.add_argument('-p', '--project', help='Specify a new or existing project name', action='store', required=False)
args = parser.parse_args()

if args.project:
    __project__.open(args.project)

c = console.Console()
c.start()

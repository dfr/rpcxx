#-
# Copyright (c) 2016-2017 Doug Rabson
# All rights reserved.
#

import os
import os.path

notice = ['Copyright (c) 2016-2017 Doug Rabson',
          'All rights reserved.']

def exclude(dir):
    if root.startswith('./third_party'):
        return True
    if root.startswith('./.git'):
        return True
    return False

def process(path, begin, middle, end):
    with open(path) as f:
        lines = list(f)
        if lines[0] == begin + '\n':
            # File already has a copyright notice - strip it out
            while lines[0] != end + '\n':
                lines = lines[1:]
            lines = lines[1:]
            if lines[0] == '\n':
                lines = lines[1:]
    with open(path, 'w') as f:
        print >>f, begin
        for line in notice:
            print >>f, '%s %s' % (middle, line)
        print >>f, end
        print >>f
        for line in lines:
            print >>f, line,

def process_c_style(path):
    print 'Processing C-style file:', path
    process(path, '/*-', ' *', ' */')

def process_shell_style(path):
    print 'Processing Shell-style file:', path
    process(path, '#-', '#', '#')

for root, dirs, files in os.walk('.'):
    if exclude(root):
        continue
    for file in files:
        if file.endswith('.cpp') or file.endswith('.c') or file.endswith('.h'):
            process_c_style(os.path.join(root, file))
        if file == 'BUILD' or file.endswith('.py'):
            process_shell_style(os.path.join(root, file))

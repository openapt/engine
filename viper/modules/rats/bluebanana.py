# Originally written by Kevin Breen (@KevTheHermit):
# https://github.com/kevthehermit/RATDecoders/blob/master/BlueBanana.py

import os
import sys
import string
from zipfile import ZipFile
from cStringIO import StringIO
from Crypto.Cipher import AES

from viper.common.out import *

def decrypt_aes(key, data):
    cipher = AES.new(key)
    return cipher.decrypt(data)

def decrypt_conf(conFile):
    key1 = '15af8sd4s1c5s511'
    key2 = '4e3f5a4c592b243f'
    first = decrypt_aes(key1, conFile.decode('hex'))
    second = decrypt_aes(key2, first[:-16].decode('hex'))
    return second
    
def extract_config(raw_conf):
    conf = {}
    clean = filter(lambda x: x in string.printable, raw_conf)
    fields = clean.split('<separator>')

    conf['Domain'] = fields[0]
    conf['Password'] = fields[1]
    conf['Port1'] = fields[2]
    conf['Port2'] = fields[3]

    if len(fields) > 4:
        conf['InstallName'] = fields[4]
        conf['JarName'] = fields[5]

    return conf

def config(data):
    new_zip = StringIO(data)
    with ZipFile(new_zip) as zip_handle:
        for name in zip_handle.namelist():
            # This file contains the encrypted config.
            if name == 'config.txt':
                conf_data = zip_handle.read(name)
    if conf_data:
        raw_conf = decrypt_conf(conf_data)
        conf = extract_config(raw_conf)

    return conf

# Originally written by Kevin Breen (@KevTheHermit):
# https://github.com/kevthehermit/RATDecoders/blob/master/HawkEye.py

import os
import sys
import string
from struct import unpack
from base64 import b64decode

#Non Standard Imports
import pype32
from Crypto.Cipher import AES
from pbkdf2 import PBKDF2


#Helper Functions Go Here

def string_clean(line):
    return ''.join((char for char in line if 32< ord(char) < 127))
    
# Crypto Stuffs
def decrypt_string(key, salt, coded):
    #try:
        # Derive key
        generator = PBKDF2(key, salt)
        aes_iv = generator.read(16)
        aes_key = generator.read(32)
        # Crypto
        mode = AES.MODE_CBC
        cipher = AES.new(aes_key, mode, IV=aes_iv)
        value = cipher.decrypt(b64decode(coded)).replace('\x00', '')
        return value#.encode('hex')
    #except:
        #return False

# Get a list of strings from a section
def get_strings(pe, dir_type):
    counter = 0
    string_list = []
    m = pe.ntHeaders.optionalHeader.dataDirectory[14].info
    for s in m.netMetaDataStreams[dir_type].info:
        for offset, value in s.iteritems():
            string_list.append(value)
        counter += 1
    return string_list
        
#Turn the strings in to a python config_dict

# Duplicate strings dont seem to be duplicated so we need to catch them
def config_1(key, salt, string_list):
    config_dict = {}
    for i in range(40):
        if len(string_list[1]) > 200:
            config_dict["Embedded File found at {0}".format(i)]
        else:
            try:
                config_dict["Crypted String {0}".format(i)] = decrypt_string(key, salt, string_list[i])
            except:
                config_dict["Config String {0}".format(i)] = string_list[i]
    return config_dict
    
def config(data):
    pe = pype32.PE(data=data)
    string_list = get_strings(pe, 2)
    key, salt = 'HawkEyeKeylogger', '3000390039007500370038003700390037003800370038003600'.decode('hex')
    config_dict = config_1(key, salt, string_list)
    return config_dict
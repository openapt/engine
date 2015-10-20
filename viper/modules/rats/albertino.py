# Originally written by Kevin Breen (@KevTheHermit):
# https://github.com/kevthehermit/RATDecoders/blob/master/Albertino.py

import re
import string
from Crypto.Cipher import DES
from base64 import b64decode


def string_print(line):
    return filter(lambda x: x in string.printable, line)

def get_config(data):
    m = re.search('\x01\x96\x01(.*)@@', data)
    raw_config = m.group(0).replace('@','')[3:]
    return raw_config
        
def decrypt_des(data):
    key = '&%#@?,:*'
    iv = '\x12\x34\x56\x78\x90\xab\xcd\xef'
    cipher = DES.new(key, DES.MODE_CBC, iv)
    return cipher.decrypt(data)


def parsed_config(clean_config):
    sections = clean_config.split('*')
    config_dict = {}
    if len(sections) == 7:
        config_dict['Version'] = '4.x'
        config_dict['Domain1'] = sections[0]
        config_dict['Domain2'] = sections[1]
        config_dict['RegKey1'] = sections[2]
        config_dict['RegKey2'] = sections[3]
        config_dict['Port1'] = sections[4]
        config_dict['Port2'] = sections[5]
        config_dict['Mutex'] = sections[6]
    if len(sections) == 5:
        config_dict['Version'] = '2.x'
        config_dict['Domain1'] = sections[0]
        config_dict['Domain2'] = sections[1]
        config_dict['Port1'] = sections[2]
        config_dict['Port2'] = sections[3]
        config_dict['AntiDebug'] = sections[4]
    return config_dict

def config(data):
    coded_config = get_config(data)
    decoded_config = b64decode(coded_config)
    raw_config = decrypt_des(decoded_config)
    clean_config = string_print(raw_config)
    return parsed_config(clean_config)

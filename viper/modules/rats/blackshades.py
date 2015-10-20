# Originally written by Brian Wallace (@botnet_hunter):
# http://blog.cylance.com/a-study-in-bots-blackshades-net

import os
import sys
import string
import re

PRNG_SEED = 0

def is_valid_config(config):
    if config[:3] != "\x0c\x0c\x0c":
        return False
    if config.count("\x0C\x0C\x0C") < 15:
        return False
    return True

def get_next_rng_value():
    global PRNG_SEED
    PRNG_SEED = ((PRNG_SEED * 1140671485 + 12820163) & 0xffffff)
    return PRNG_SEED / 65536

def decrypt_configuration(hex):
    global PRNG_SEED
    ascii = hex.decode('hex')
    tail = ascii[0x20:]

    pre_check = []
    for x in xrange(3):
        pre_check.append(ord(tail[x]) ^ 0x0c)

    for x in xrange(0xffffff):
        PRNG_SEED = x
        if get_next_rng_value() != pre_check[0] or get_next_rng_value() != pre_check[1] or get_next_rng_value() != pre_check[2]:
            continue
        PRNG_SEED = x
        config = "".join((chr(ord(c) ^ int(get_next_rng_value())) for c in tail))
        if is_valid_config(config):
            return config.split("\x0c\x0c\x0c")

def config_extract(raw_data):
    config_pattern = re.findall('[0-9a-fA-F]{154,}', raw_data)
    for s in config_pattern:
        if (len(s) % 2) == 1:
            s = s[:-1]
        return s

def config_parser(config):
    config_dict = {}
    config_dict['Domain'] = config[1]
    config_dict['Client Control Port'] = config[2]
    config_dict['Client Transfer Port'] = config[3]
    config_dict['Campaign ID'] = config[4]
    config_dict['File Name'] = config[5]
    config_dict['Install Path'] = config[6]
    config_dict['Registry Key'] = config[7]
    config_dict['ActiveX Key'] = config[8]
    config_dict['Install Flag'] = config[9]
    config_dict['Hide File'] = config[10]
    config_dict['Melt File'] = config[11]
    config_dict['Delay'] = config[12]
    config_dict['USB Spread'] = config[13]
    config_dict['Mutex'] = config[14]
    config_dict['Log File'] = config[15]
    config_dict['Folder Name'] = config[16]
    config_dict['Smart DNS'] = config[17]
    config_dict['Protect Process'] = config[18]
    return config_dict
        
def config(data):
    raw_config = config_extract(data)
    if raw_config:
        config = decrypt_configuration(raw_config)
        if config and len(config) > 15:
            sorted_config = config_parser(config)
            return sorted_config

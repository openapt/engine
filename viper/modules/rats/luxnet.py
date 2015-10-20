# Originally written by Kevin Breen (@KevTheHermit):
# https://github.com/kevthehermit/RATDecoders/blob/master/Albertino.py

import re
import string
from struct import unpack

def calc_length(byteStr):
    #try:
        return unpack(">H", byteStr)[0]
    #except:
        #return None

def string_print(line):
    return filter(lambda x: x in string.printable, line)

def first_split(data):
    split_strings = ['\x5B\x53\x00\x6F\x00\x66\x00\x74\x00\x77\x00\x61\x00\x72\x00\x65\x00\x5C\x00\x4D\x00\x69\x00\x63\x00\x72\x00\x6F\x00\x73\x00\x6F\x00\x66\x00\x74\x00\x5C\x00\x57\x00\x69\x00\x6E\x00\x64\x00\x6F\x00\x77\x00\x73\x00\x5C\x00\x43\x00\x75\x00\x72\x00\x72\x00\x65\x00\x6E\x00\x74\x00\x56\x00\x65\x00\x72\x00\x73\x00\x69\x00\x6F\x00\x6E\x00\x5C\x00\x52\x00\x75\x00\x6E\x00', '\x79\x55\x00\x32\x00\x39\x00\x6D\x00\x64\x00\x48\x00\x64\x00\x68\x00\x63\x00\x6D\x00\x56\x00\x63\x00\x54\x00\x57\x00\x6C\x00\x6A\x00\x63\x00\x6D\x00\x39\x00\x7A\x00\x62\x00\x32\x00\x5A\x00\x30\x00\x58\x00\x46\x00\x64\x00\x70\x00\x62\x00\x6D\x00\x52\x00\x76\x00\x64\x00\x33\x00\x4E\x00\x63\x00\x51\x00\x33\x00\x56\x00\x79\x00\x63\x00\x6D\x00\x56\x00\x75\x00\x64\x00\x46\x00\x5A\x00\x6C\x00\x63\x00\x6E\x00\x4E\x00\x70\x00\x62\x00\x32\x00\x35\x00\x63\x00\x55\x00\x6E\x00\x56\x00\x75\x00']
    for split_string in split_strings:
        splits = data.split(split_string)
        if len(splits) == 2:
            return splits[1]

    
def data_walk(split_data):
    stringList = []
    offset = 0
    config = bytearray(split_data)
    count = 0
    while offset < len(config) and count < 2:
        if str(config[offset]) == '1':
            len_bytes = '{0}{1}'.format(chr(0),chr(config[offset+1]))
        else:
            len_bytes = str(config[offset:offset+2])
        new_length = calc_length(len_bytes)
        that = config[offset+2:offset+int(new_length)]
        stringList.append(str(that.replace("\x00", "")))
        offset += int(new_length+1)
        count += 1
    return stringList

def parse_config(raw_config):
    conf_dict = {}
    conf_dict['Domain'] = raw_config[0]
    conf_dict['Port'] = raw_config[1]
    return conf_dict

def config(data):
    get_config = first_split(data)
    if get_config:
        raw_config = data_walk(get_config)
        config_dict = parse_config(raw_config)
        return config_dict

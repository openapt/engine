# Originally written by Kevin Breen (@KevTheHermit):
# https://github.com/kevthehermit/RATDecoders/blob/master/ClientMesh.py

import re
import string
from base64 import b64decode


def stringPrintable(line):
    return filter(lambda x: x in string.printable, line)

def first_split(data):
    splits = data.split('\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x7e')
    if len(splits) == 2:
        return splits[1]


def base64_deocde(b64_string):
    return b64decode(b64_string)

    
def conf_extract(coded_config):
    conf_list = []
    decoded_conf = base64_deocde(coded_config)
    split_list = decoded_conf.split('``')
    for conf in split_list:
        conf_list.append(conf)
    return conf_list


def process_config(raw_config):
    conf_dict = {}
    conf_dict['Domain'] = raw_config[0]
    conf_dict['Port'] = raw_config[1]
    conf_dict['Password'] = raw_config[2]
    conf_dict['CampaignID'] = raw_config[3]
    conf_dict['MsgBoxFlag'] = raw_config[4]
    conf_dict['MsgBoxTitle'] = raw_config[5]
    conf_dict['MsgBoxText'] = raw_config[6]
    conf_dict['Startup'] = raw_config[7]
    conf_dict['RegistryKey'] = raw_config[8]
    conf_dict['RegistryPersistance'] = raw_config[9]
    conf_dict['LocalKeyLogger'] = raw_config[10]
    conf_dict['VisibleFlag'] = raw_config[11]
    conf_dict['Unknown'] = raw_config[12]
    return conf_dict

def config(data):
        coded_config = first_split(data)
        raw_config = conf_extract(coded_config)
        final_config = process_config(raw_config)
        return final_config

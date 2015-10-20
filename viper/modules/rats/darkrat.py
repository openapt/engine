# Originally written by Kevin Breen (@KevTheHermit):
# https://github.com/kevthehermit/RATDecoders/blob/master/Albertino.py

import re
import string


def string_print(line):
    return filter(lambda x: x in string.printable, line)


def config(data):
	config_dict = {}
	raw_config = data.split('@1906dark1996coder@')
	if len(raw_config) > 3:
		config_dict['Domain'] = raw_config[1][7:-1]
		config_dict['AutoRun'] = raw_config[2]
		config_dict['USB Spread'] = raw_config[3]
		config_dict['Hide Form'] = raw_config[4]
		config_dict['Msg Box Title'] = raw_config[6]
		config_dict['Msg Box Text'] = raw_config[7]
		config_dict['Timer Interval'] = raw_config[8]
		if raw_config[5] == 4:
			config_dict['Msg Box Type'] = 'Information'
		elif raw_config[5] == 2:
			config_dict['Msg Box Type'] = 'Question'
		elif raw_config[5] == 3:
			config_dict['Msg Box Type'] = 'Exclamation'
		elif raw_config[5] == 1:
			config_dict['Msg Box Type'] = 'Critical'
		else:
			config_dict['Msg Box Type'] = 'None'
		return config_dict

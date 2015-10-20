# Originally written by Kevin Breen (@KevTheHermit):
# https://github.com/kevthehermit/RATDecoders/blob/master/AdWind.py

import string
import binascii
from zipfile import ZipFile
from cStringIO import StringIO
from Crypto.Cipher import ARC4, DES
import xml.etree.ElementTree as ET


def sortConfig(old_config):
    if old_config['Version'] == 'Adwind RAT v1.0':
        new_config = {}
        new_config['Version'] = old_config['Version']
        new_config['Delay'] = old_config['delay']
        new_config['Domain'] = old_config['dns']
        new_config['Install Flag'] = old_config['instalar']
        new_config['Jar Name'] = old_config['jarname']
        new_config['Reg Key'] = old_config['keyClase']
        new_config['Install Folder'] = old_config['nombreCarpeta']
        new_config['Password'] = old_config['password']
        new_config['Campaign ID'] = old_config['prefijo']
        new_config['Port1'] = old_config['puerto1']
        new_config['Port2'] = old_config['puerto2']
        new_config['Reg Value'] = old_config['regname']
        return new_config

    if old_config['Version'] == 'Adwind RAT v2.0':
        new_config = {}
        new_config['Version'] = old_config['Version']
        new_config['Delay'] = old_config['delay']
        new_config['Domain'] = old_config['dns']
        new_config['Install Flag'] = old_config['instalar']
        new_config['Reg Key'] = old_config['keyClase']
        new_config['Password'] = old_config['password']
        new_config['Campaign ID'] = old_config['prefijo']
        new_config['Port1'] = old_config['puerto']
        return new_config
    return old_config
        
def decrypt_des(enckey, data):
    cipher = DES.new(enckey, DES.MODE_ECB) # set the ciper
    return cipher.decrypt(data) # decrpyt the data
    
def decrypt_rc4(enckey, data):
    cipher = ARC4.new(enckey) # set the ciper
    return cipher.decrypt(data) # decrpyt the data

def config(data):
    Key = "awenubisskqi"
    newZip = StringIO(data)
    raw_config = {}
    with ZipFile(newZip, 'r') as zip:
        for name in zip.namelist():
            if name == "config.xml": # contains the encryption key
                # We need two attempts here first try DES for V1 If not try RC4 for V2
                try:
                    config = zip.read(name)
                    result = decrypt_des(Key[:-4], config)
                except:
                    config = zip.read(name)
                    result = decrypt_rc4(Key, config)                                
                xml = filter(lambda x: x in string.printable, result)
                root = ET.fromstring(xml)
                for child in root:
                    if child.text.startswith("Adwind RAT"):
                        raw_config['Version'] = child.text
                    else:
                        raw_config[child.attrib['key']] = child.text
                new_config = sortConfig(raw_config)
                return new_config

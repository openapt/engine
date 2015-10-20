# Originally written by Kevin Breen (@KevTheHermit):
# https://github.com/kevthehermit/RATDecoders/blob/master/Xtreme.py

from struct import unpack

import pefile

from viper.common.out import *

def get_unicode_string(buf,pos):
    out = ''
    for i in range(len(buf[pos:])):
        if not (ord(buf[pos+i]) >= 32 and ord(buf[pos+i]) <= 126) and not (ord(buf[pos+i+1]) >= 32 and ord(buf[pos+i+1]) <= 126):
            out += '\x00'
            break
        out += buf[pos+i]
    if out == '':
        return None
    else:
        return out.replace('\x00','')

def rc4crypt(data, key):
    x = 0
    box = range(256)
    for i in range(256):
        x = (x + box[i] + ord(key[i % 6])) % 256
        box[i], box[x] = box[x], box[i]
    x = 0
    y = 0
    out = []
    for char in data:
        x = (x + 1) % 256
        y = (y + box[x]) % 256
        box[x], box[y] = box[y], box[x]
        out.append(chr(ord(char) ^ box[(box[x] + box[y]) % 256]))
    
    return ''.join(out)

def extract_config(rawData):
    try:
        pe = pefile.PE(data=rawData)
        try:
          rt_string_idx = [
          entry.id for entry in 
          pe.DIRECTORY_ENTRY_RESOURCE.entries].index(pefile.RESOURCE_TYPE['RT_RCDATA'])
        except ValueError, e:
            return None
        except AttributeError, e:
            return None
        rt_string_directory = pe.DIRECTORY_ENTRY_RESOURCE.entries[rt_string_idx]
        for entry in rt_string_directory.directory.entries:
            if str(entry.name) == 'XTREME':
                data_rva = entry.directory.entries[0].data.struct.OffsetToData
                size = entry.directory.entries[0].data.struct.Size
                data = pe.get_memory_mapped_image()[data_rva:data_rva+size]
                return data
    except:
        return None 

def v29(rawConfig):
    config = {}
    config["ID"] = getUnicodeString(rawConfig, 0x9e0)
    config["Group"] = getUnicodeString(rawConfig, 0xa5a)
    config["Version"] = getUnicodeString(rawConfig, 0xf2e)
    config["Mutex"] = getUnicodeString(rawConfig, 0xfaa)
    config["Install Dir"] = getUnicodeString(rawConfig, 0xb50)
    config["Install Name"] = getUnicodeString(rawConfig, 0xad6)
    config["HKLM"] = getUnicodeString(rawConfig, 0xc4f)
    config["HKCU"] = getUnicodeString(rawConfig, 0xcc8)
    config["Custom Reg Key"] = getUnicodeString(rawConfig, 0xdc0)
    config["Custom Reg Name"] = getUnicodeString(rawConfig, 0xe3a)
    config["Custom Reg Value"] = getUnicodeString(rawConfig, 0xa82)
    config["ActiveX Key"] = getUnicodeString(rawConfig, 0xd42)
    config["Injection"] = getUnicodeString(rawConfig, 0xbd2)
    config["FTP Server"] = getUnicodeString(rawConfig, 0x111c)
    config["FTP UserName"] = getUnicodeString(rawConfig, 0x1210)
    config["FTP Password"] = getUnicodeString(rawConfig, 0x128a)
    config["FTP Folder"] = getUnicodeString(rawConfig, 0x1196)
    config["Domain1"] = str(getUnicodeString(rawConfig, 0x50)+":"+str(unpack("<I",rawConfig[0:4])[0]))
    config["Domain2"] = str(getUnicodeString(rawConfig, 0xca)+":"+str(unpack("<I",rawConfig[4:8])[0]))
    config["Domain3"] = str(getUnicodeString(rawConfig, 0x144)+":"+str(unpack("<I",rawConfig[8:12])[0]))
    config["Domain4"] = str(getUnicodeString(rawConfig, 0x1be)+":"+str(unpack("<I",rawConfig[12:16])[0]))
    config["Domain5"] = str(getUnicodeString(rawConfig, 0x238)+":"+str(unpack("<I",rawConfig[16:20])[0]))
    config["Domain6"] = str(getUnicodeString(rawConfig, 0x2b2)+":"+str(unpack("<I",rawConfig[20:24])[0]))
    config["Domain7"] = str(getUnicodeString(rawConfig, 0x32c)+":"+str(unpack("<I",rawConfig[24:28])[0]))
    config["Domain8"] = str(getUnicodeString(rawConfig, 0x3a6)+":"+str(unpack("<I",rawConfig[28:32])[0]))
    config["Domain9"] = str(getUnicodeString(rawConfig, 0x420)+":"+str(unpack("<I",rawConfig[32:36])[0]))
    config["Domain10"] = str(getUnicodeString(rawConfig, 0x49a)+":"+str(unpack("<I",rawConfig[36:40])[0]))
    config["Domain11"] = str(getUnicodeString(rawConfig, 0x514)+":"+str(unpack("<I",rawConfig[40:44])[0]))
    config["Domain12"] = str(getUnicodeString(rawConfig, 0x58e)+":"+str(unpack("<I",rawConfig[44:48])[0]))
    config["Domain13"] = str(getUnicodeString(rawConfig, 0x608)+":"+str(unpack("<I",rawConfig[48:52])[0]))
    config["Domain14"] = str(getUnicodeString(rawConfig, 0x682)+":"+str(unpack("<I",rawConfig[52:56])[0]))
    config["Domain15"] = str(getUnicodeString(rawConfig, 0x6fc)+":"+str(unpack("<I",rawConfig[56:60])[0]))
    config["Domain16"] = str(getUnicodeString(rawConfig, 0x776)+":"+str(unpack("<I",rawConfig[60:64])[0]))
    config["Domain17"] = str(getUnicodeString(rawConfig, 0x7f0)+":"+str(unpack("<I",rawConfig[64:68])[0]))
    config["Domain18"] = str(getUnicodeString(rawConfig, 0x86a)+":"+str(unpack("<I",rawConfig[68:72])[0]))
    config["Domain19"] = str(getUnicodeString(rawConfig, 0x8e4)+":"+str(unpack("<I",rawConfig[72:76])[0]))
    config["Domain20"] = str(getUnicodeString(rawConfig, 0x95e)+":"+str(unpack("<I",rawConfig[76:80])[0]))
    return config

def v32(rawConfig):
    config = {}
    config["ID"] = getUnicodeString(rawConfig, 0x1b4)
    config["Group"] = getUnicodeString(rawConfig, 0x1ca)
    config["Version"] = getUnicodeString(rawConfig, 0x2bc)
    config["Mutex"] = getUnicodeString(rawConfig, 0x2d4)
    config["Install Dir"] = getUnicodeString(rawConfig, 0x1f8)
    config["Install Name"] = getUnicodeString(rawConfig, 0x1e2)
    config["HKLM"] = getUnicodeString(rawConfig, 0x23a)
    config["HKCU"] = getUnicodeString(rawConfig, 0x250)
    config["ActiveX Key"] = getUnicodeString(rawConfig, 0x266)
    config["Injection"] = getUnicodeString(rawConfig, 0x216)
    config["FTP Server"] = getUnicodeString(rawConfig, 0x35e)
    config["FTP UserName"] = getUnicodeString(rawConfig, 0x402)
    config["FTP Password"] = getUnicodeString(rawConfig, 0x454)
    config["FTP Folder"] = getUnicodeString(rawConfig, 0x3b0)
    config["Domain1"] = str(getUnicodeString(rawConfig, 0x14)+":"+str(unpack("<I",rawConfig[0:4])[0]))
    config["Domain2"] = str(getUnicodeString(rawConfig, 0x66)+":"+str(unpack("<I",rawConfig[4:8])[0]))
    config["Domain3"] = str(getUnicodeString(rawConfig, 0xb8)+":"+str(unpack("<I",rawConfig[8:12])[0]))
    config["Domain4"] = str(getUnicodeString(rawConfig, 0x10a)+":"+str(unpack("<I",rawConfig[12:16])[0]))
    config["Domain5"] = str(getUnicodeString(rawConfig, 0x15c)+":"+str(unpack("<I",rawConfig[16:20])[0]))
    config["Msg Box Title"] = getUnicodeString(rawConfig, 0x50c)
    config["Msg Box Text"] = getUnicodeString(rawConfig, 0x522)
    return config

def v35(config_raw):
    config = {}
    config['ID'] = get_unicode_string(config_raw, 0x1b4)
    config['Group'] = get_unicode_string(config_raw, 0x1ca)
    config['Version'] = get_unicode_string(config_raw, 0x2d8)
    config['Mutex'] = get_unicode_string(config_raw, 0x2f0)
    config['Install Dir'] = get_unicode_string(config_raw, 0x1f8)
    config['Install Name'] = get_unicode_string(config_raw, 0x1e2)
    config['HKLM'] = get_unicode_string(config_raw, 0x23a)
    config['HKCU'] = get_unicode_string(config_raw, 0x250)
    config['ActiveX Key'] = get_unicode_string(config_raw, 0x266)
    config['Injection'] = get_unicode_string(config_raw, 0x216)
    config['FTP Server'] = get_unicode_string(config_raw, 0x380)
    config['FTP UserName'] = get_unicode_string(config_raw, 0x422)
    config['FTP Password'] = get_unicode_string(config_raw, 0x476)
    config['FTP Folder'] = get_unicode_string(config_raw, 0x3d2)
    config['Domain1'] = str(get_unicode_string(config_raw, 0x14)+':'+str(unpack('<I',config_raw[0:4])[0]))
    config['Domain2'] = str(get_unicode_string(config_raw, 0x66)+':'+str(unpack('<I',config_raw[4:8])[0]))
    config['Domain3'] = str(get_unicode_string(config_raw, 0xb8)+':'+str(unpack('<I',config_raw[8:12])[0]))
    config['Domain4'] = str(get_unicode_string(config_raw, 0x10a)+':'+str(unpack('<I',config_raw[12:16])[0]))
    config['Domain5'] = str(get_unicode_string(config_raw, 0x15c)+':'+str(unpack('<I',config_raw[16:20])[0]))
    config['Msg Box Title'] = get_unicode_string(config_raw, 0x52c)
    config['Msg Box Text'] = get_unicode_string(config_raw, 0x542)
    return config

def config(data):
    key = 'C\x00O\x00N\x00F\x00I\x00G'

    config_coded = extract_config(data)
    config_raw = rc4crypt(config_coded, key)

    # 1.3.x - Not implemented yet.
    if len(config_raw) == 0xe10:
        print_warning("Detected XtremeRAT 1.3.x, not supported yet")
        config = None
    # 2.9.x - Not a stable extract.
    elif len(config_raw) == 0x1390 or len(config_raw) == 0x1392:
        config = v29(config_raw)
    # 3.1 & 3.2
    elif len(config_raw) == 0x5Cc:
        config = v32(config_raw)
    # 3.5
    elif len(config_raw) == 0x7f0:
        config = v35(config_raw)
    else:
        print_error("No known XtremeRAT version detected")
        config = None

    return config

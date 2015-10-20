#!/usr/bin/env python
# This file is part of Viper - https://github.com/viper-framework/viper
# See the file 'LICENSE' for copying permission.

import os
import re
import sys
import json
import time
import bottle
import shutil
import logging
try:
    from subprocess import getoutput
except ImportError:
    from commands import getoutput
    
try:
    from scandir import walk
except ImportError:
    from os import walk
    
import requests
import argparse
import tempfile
import contextlib
import tarfile

from zipfile import ZipFile
from gzip import GzipFile
from bz2 import BZ2File
from bottle import route, request, response, run, get, template, static_file, redirect

from viper.core.session import __sessions__
from viper.core.plugins import __modules__
from viper.core.project import __project__
from viper.common.objects import File
from viper.core.storage import store_sample, get_sample_path
from viper.core.database import Database
from viper.common import network
from viper.core.ui.commands import Commands
from viper.common.constants import VIPER_ROOT

###
# Needed for bottle (and correct uwsgi handling)
###
app = application = bottle.default_app()


##
# User Config
##

web_port = 9090
cuckoo_api = 'http://localhost:8090'
cuckoo_web = 'http://localhost:9191'
# VIPER_ROOT ='/srv/www/viper' #if not set when using uwsgi, web.py does not correctly set VIPER_ROOT
##
# End User Config
##

bottle.TEMPLATE_PATH.insert(0,os.path.join(VIPER_ROOT, 'data/web'))

# Module Dicts
mod_dicts = {}
mod_dicts['apk'] = {'help':'-h','info':'-i', 'perm':'-p', 'list':'-f', 'all':'-a', 'dump':'-d'}
mod_dicts['clamav'] = {'run':''}
mod_dicts['debup'] = {'info':'', 'extract':'-s'}
mod_dicts['editdistance'] = {'run':''}
mod_dicts['elf'] = {'sections':'--sections', 'segments':'--segments', 'symbols':'--symbols', 'interp':'--interpreter', 'dynamic':'--dynamic'}
mod_dicts['email'] = {'envelope': '-e', 'attach': '-f', 'header':'-r', 'trace':'-t', 'traceall':'-T', 'spoof':'-s', 'all':'-a'}
mod_dicts['exif'] = {'run':''}
mod_dicts['fuzzy'] = {'run':''}
mod_dicts['html'] = {'scripts':'-s', 'links':'-l', 'iframe':'-f', 'embed':'-e', 'images':'-i', 'dump':'-d'}
mod_dicts['idx'] = {'run':''}
mod_dicts['image'] = {'ghiro':'--ghiro'}
mod_dicts['jar'] = {'run':''}
mod_dicts['office'] = {'meta':'-m', 'oleid':'-o', 'streams':'-s', 'export':'-e'}
mod_dicts['pdf'] = {'id':'id', 'streams':'streams'}
mod_dicts['pe'] = {'imports':'imports', 'exports':'exports', 'res':'resources', 'imp':'imphash', 'compile':'compiletime', 'peid':'peid', 'security':'security', 'language':'language', 'sections':'sections', 'pehash':'pehash'}
mod_dicts['rat'] = {'auto':'-a', 'list': '-l'}
mod_dicts['reports'] = {'malwr':'--malwr', 'anubis':'--anubis', 'threat':'--threat', 'joe':'--joe', 'meta':'--meta'}
mod_dicts['shellcode'] = {'run':''}
mod_dicts['strings'] = {'all':'-a', 'hosts':'-H'}
mod_dicts['swf'] = {'decom':'decompress'}
mod_dicts['virustotal'] = {'scan':'', 'submit':'-s'}
mod_dicts['xor'] = {'xor':'', 'rot':'-r', 'all':'-a', 'export':'-o'}
mod_dicts['yara'] = {'scan':'scan -t', 'all': 'scan -a -t'}


##
# Helper Functions
#

def parse(data):
    root = ''
    args = []
    # Split words by white space.
    words = data.split()
    # First word is the root command.
    root = words[0]
    # If there are more words, populate the arguments list.
    if len(words) > 1:
        args = words[1:]
    return (root, args)


def print_output(output):
    if not output:
        return '<p class="text-danger">! The command Generated no Output</p>' 
    return_html = ''
    for entry in output:
        # Skip lines that say seesion opened
        if 'Session opened on' in entry['data']:
            continue
        if entry['type'] == 'info':
            return_html += '<p class="text-primary">{0}</p>'.format(entry['data'])
            #self.log('info', entry['data'])
        elif entry['type'] == 'item':
            return_html += '<li class="text-primary">{0}</li>'.format(entry['data'])
        elif entry['type'] == 'warning':
            return_html += '<p class="text-warning">{0}</p>'.format(entry['data'])
        elif entry['type'] == 'error':
            return_html += '<p class="text-danger">{0}</p>'.format(entry['data'])
        elif entry['type'] == 'success':
            return_html += '<p class="text-success">{0}</p>'.format(entry['data'])
        elif entry['type'] == 'table':
            # set the table
            return_html += '<table class="table table-bordered">'
            # Column Titles
            return_html += '<tr>'
            for column in entry['data']['header']:
                return_html += '<th>{0}</th>'.format(column)
            return_html += '</tr>'
            # Rows
            for row in entry['data']['rows']:
                return_html += '<tr>'
                for cell in row:
                    return_html += '<td>{0}</td>'.format(cell)
                return_html += '</tr>'
            # Close table
            return_html += '</table>'
        else:
            return_html += '<p>{0}</p>'.format(entry['data'])
    return return_html
    
def parse_text(module_text):
    # String to hold the new text
    set_text = ''
    # Split in to lines.
    for line in module_text.split('\n'):
        # Remove the colour codes
        line = re.sub('\[(\d)+m', '', line.replace('\x1b', ''))
        # Ignore the line that says we opened a session
        if 'Session opened on' in line:
            continue
        # add text the string
        set_text += '{0}\n'.format(line)
    return set_text

def project_list():
    # Get a list of projects 
    projects_path = os.path.join(os.getcwd(), 'projects')
    p_list = []
    if os.path.exists(projects_path):
        for project in os.listdir(projects_path):
            project_path = os.path.join(projects_path, project)
            if os.path.isdir(project_path):
                p_list.append(project)
    return p_list

# this will allow complex command line parameters to be passed in via the web gui    
def module_cmdline(cmd_line, file_hash):
    html = ""
    cmd = Commands()
    split_commands = cmd_line.split(';')
    for split_command in split_commands:
        split_command = split_command.strip()
        if not split_command:
            continue
        root, args = parse(split_command)
        try:
            if root in cmd.commands:
                cmd.commands[root]['obj'](*args)
                html += print_output(cmd.output)
                del(cmd.output[:])
            elif root in __modules__:
                # if prev commands did not open a session open one on the current file
                if file_hash:
                    path = get_sample_path(file_hash)
                    __sessions__.new(path)
                module = __modules__[root]['obj']()
                module.set_commandline(args)
                module.run()

                html += print_output(module.output)
                del(module.output[:])
            else:
                html += '<p class="text-danger">{0} is not a valid command</p>'.format(cmd_line)
        except:
            html += '<p class="text-danger">We were unable to complete the command {0}</p>'.format(cmd_line)
    __sessions__.close()
    return html        
        
    
def module_text(cmd_string, file_hash):
    # A lot of commands rely on an open session
    # open a session on the file hash
    path = get_sample_path(file_hash)
    __sessions__.new(path)
    # Run the Module with args
    if __sessions__.is_set():
        root, args = parse(cmd_string)
        if root in __modules__:
            module = __modules__[root]['obj']()
            module.set_commandline(args)
            module.run()
            html = print_output(module.output)
            del(module.output[:])
        else:
            html = '<p class="text-danger">{0} is not a valid command</p>'.format(cmd_string)
    else:
        '<p class="text-danger">! There is no open session</p>'
    # close the session
    __sessions__.close()
    return html

# context manager for file uploader   
@contextlib.contextmanager
def upload_temp():
    temp_dir = tempfile.mkdtemp()
    yield temp_dir
    shutil.rmtree(temp_dir)
       
##
# Pages
#

#Returns Static files e.g. CSS / JS
@get('/static/:path#.+#')
def server_static(path):
    return static_file(path, root=os.path.join(VIPER_ROOT, 'data/web/static'))

# Index Page
@app.route("/")
@app.route("/project/<p>")
def landing(p=False):
    contents = {}
    if p in project_list():
        __project__.open(p)
        contents['p'] = p
    else:
        __project__.open("../")
        contents['p'] = 'Main'
    db = Database() 
    # Pagination
    # 25 per page
    value = 25
    offset = 0
    contents['count'] = db.get_sample_count()
    page = request.query.page
    if not page:
        page = 0
    offset = int(page) * int(value) 
    contents['act_page'] = page   
    contents['latest'] = db.find('latest', value=value, offset=offset)
    # return the Template
    return template('index.tpl', **contents)

# create Project
@app.route("/create", method="POST")
def add_project():
    project_name = request.forms.get('project').strip()
    __project__.open(project_name)
    redirect('/project/{0}'.format(project_name))
       
# Info Page for File
@app.route("/file/<file_hash>", method="GET")
@app.route("/file/<project>/<file_hash>", method="GET")
def file_info(file_hash, project=False):
    contents = {}
    if project in project_list():
        __project__.open(project)
        contents['project'] = project
    else:
        __project__.open('../')
        contents['project'] = 'Main'
    # Open the Database
    db = Database()
    # Open a session
    try:
        path = get_sample_path(file_hash)
        __sessions__.new(path)
    except:
        return template('error.tpl', error="{0} Does not match any hash in the Database".format(file_hash))
    
    # Get the file info
    contents['file_info'] = [
                __sessions__.current.file.name,
                __sessions__.current.file.tags,
                __sessions__.current.file.path,
                __sessions__.current.file.size,
                __sessions__.current.file.type,
                __sessions__.current.file.mime,
                __sessions__.current.file.md5,
                __sessions__.current.file.sha1,
                __sessions__.current.file.sha256,
                __sessions__.current.file.sha512,
                __sessions__.current.file.ssdeep,
                __sessions__.current.file.crc32                
                ]
                
    # Get Any Notes
    note_list = []
    malware = db.find(key='sha256', value=file_hash)
    if malware:
        notes = malware[0].note
        if notes:
            rows = []
            for note in notes:
                note_list.append([note.title, note.body, note.id])
    contents['notes'] = note_list
    
    # Close the session
    __sessions__.close()
    # Return the page
    return template('file.tpl', **contents)
    
# Add New File
# Uses Context Manager to Remove Temp files
@app.route('/add', method='POST')
def add_file():
    tags = request.forms.get('tag_list')
    uploads = request.files.getlist('file')
    
    # Set Project
    project = request.forms.get('project')
    if project in project_list():
        __project__.open(project)
    else:
        __project__.open('../')
        project = 'Main'
    db = Database()    
    file_list = []
    # Write temp file to disk
    with upload_temp() as temp_dir:
        for upload in uploads:
            file_path = os.path.join(temp_dir, upload.filename)
            with open(file_path, 'w') as tmp_file:
                tmp_file.write(upload.file.read())
            # Zip Files
            if request.forms.get('compression') == 'zip':
                zip_pass = request.forms.get('zip_pass')
                try:
                    with ZipFile(file_path) as zf:
                        zf.extractall(temp_dir, pwd=zip_pass)            
                    for root, dirs, files in walk(temp_dir, topdown=False):
                        for name in files:
                            if not name == upload.filename:
                                file_list.append(os.path.join(root, name))
                except Exception as e:
                    return template('error.tpl', error="Error with zipfile - {0}".format(e))
            # GZip Files
            elif request.forms.get('compression') == 'gz':
                try:
                    gzf = GzipFile(file_path, 'rb')
                    decompress = gzf.read()
                    gzf.close()
                    with open(file_path[:-3],"wb") as df:
                        df.write(decompress)
                    file_list.append(file_path[:-3])
                except Exception as e:
                    return template('error.tpl', error="Error with gzipfile - {0}".format(e))
            # BZip2 Files
            elif request.forms.get('compression') == 'bz2':
                try:
                    bz2f = BZ2File(file_path, 'rb')
                    decompress = bz2f.read()
                    bz2f.close()
                    with open(file_path[:-3],"wb") as df:
                        df.write(decompress)
                    file_list.append(file_path[:-3])
                except Exception as e:
                    return template('error.tpl', error="Error with bzip2file - {0}".format(e))
            # Tar Files (any, including tar.gz tar.bz2)
            elif request.forms.get('compression') == 'tar':
                try:
                    if not tarfile.is_tarfile(file_path):
                        return template('error.tpl', error="This is not a tar file")
                    with tarfile.open(file_path,'r:*') as tarf:
                        tarf.extractall(temp_dir)
                    for root, dirs, files in walk(temp_dir, topdown=False):
                        for name in files:
                            if not name == upload.filename:
                                file_list.append(os.path.join(root, name))
                except Exception as e:
                    return template('error.tpl', error="Error with tarfile - {0}".format(e))
            # Non zip files
            elif request.forms.get('compression') == 'none':
                file_list.append(file_path)
            
        # Add each file
        for new_file in file_list:
            print new_file
            obj = File(new_file)
            new_path = store_sample(obj)
            success = True
            if new_path:
                # Add file to the database.
                success = db.add(obj=obj, tags=tags)
                if not success:
                    return template('error.tpl', error="Unable to Store The File: {0}".format(upload.filename))
    redirect("/project/{0}".format(project))

#add file from url
@app.route('/URLDownload', method='POST')
def url_download():
    url = request.forms.get('url')
    tags = request.forms.get('tag_list')
    tags = "url,"+tags
    if request.forms.get("tor"):
        upload = network.download(url,tor=True)
    else:
        upload = network.download(url,tor=False)
    if upload == None:
        return template('error.tpl', error="server can't download from URL")
    # Set Project
    project = 'Main'
    db = Database()
    tf = tempfile.NamedTemporaryFile()
    tf.write(upload)
    if tf == None:
        return template('error.tpl', error="server can't download from URL")
    tf.flush()
    tf_obj = File(tf.name)
    tf_obj.name = tf_obj.sha256
    new_path = store_sample(tf_obj)
    success = False
    if new_path:
        # Add file to the database.
        success = db.add(obj=tf_obj, tags=tags)

    if success:
        #redirect("/project/{0}".format(project))
        redirect("/file/Main/"+tf_obj.sha256)
    else:
        return template('error.tpl', error="Unable to Store The File,already in database")



# File Download
@app.route("/get/<file_hash>", method="GET")
@app.route("/get/<project>/<file_hash>", method="GET")
def file_download(file_hash, project=False):
    if project in project_list():
        __project__.open(project)
    else:
        __project__.open('../')
        project = 'Main'
    # Open the Database
    db = Database()
    # Open a session
    rows = db.find(key='sha256', value=file_hash)
    if not rows:
        return template('error.tpl', error="{0} Does not match any hash in the Database".format(file_hash))
        
    path = get_sample_path(rows[0].sha256)
    if not path:
        return template('error.tpl', error="File not found on disk")

    response.content_length = os.path.getsize(path)
    response.content_type = 'application/octet-stream; charset=UTF-8'
    data = ''
    for chunk in File(path).get_chunks():
        data += chunk
    return data
    
# Search
@app.route('/search', method='POST')
def find_file():
    key = request.forms.get('key')
    value = request.forms.get('term').lower()
    project_search = request.forms.get('project')
    curr_project = request.forms.get('curr_project')
    results = {}
    projects = []
    if project_search:
        # Get list of project paths
        projects_path = os.path.join(os.getcwd(), 'projects')
        if os.path.exists(projects_path):
            for name in os.listdir(projects_path):
                projects.append(name)
        projects.append('../')
    else:
        # If not searching all projects what are we searching
        if curr_project == 'Main':
            projects.append('../')
        else:
            projects.append(curr_project)
    
    # Search each Project in the list
    for project in projects:
        __project__.open(project)
        # Init DB
        db = Database()
        #get results
        proj_results = []
        rows = db.find(key=key, value=value)
        for row in rows:
            if project == '../':
                project = 'Main'
            proj_results.append([row.name, row.sha256])
        results[project] = proj_results
 
    return template('search.tpl', results=results)


# Tags
@app.route('/tags', method='GET')
@app.route('/tags/<tag_action>', method='POST')    
def tags(tag_action=False):
    # Set DB
    db = Database()
    
    # Search or Delete
    if request.method == 'GET':
        action = request.query.action
        value = request.query.value.strip()
        
        if value:
            if action == 'search':
                # This will search all projects
                # Get project list
                projects = project_list()
                # Add Main db to list.
                projects.append('../')
                # Search All projects
                p_list = []
                results = {}
                for project in projects:
                    __project__.open(project)
                    # Init DB
                    db = Database()
                    #get results
                    proj_results = []
                    rows = db.find(key='tag', value=value)
                    for row in rows:
                        if project == '../':
                            project = 'Main'
                        proj_results.append([row.name, row.sha256])
                    results[project] = proj_results
                    p_list.append(project)
                # Return the search template
                return template('search.tpl', projects=p_list, results=results)
            else:
                return template('error.tpl', error="'{0}' Is not a valid tag action".format(action))
                             
    # Add / Delete                        
    if request.method == 'POST':
        file_hash = request.forms.get('sha256')
        project = request.forms.get('project')
        tag_name = request.forms.get('tag')
        if tag_action == 'add':
            if file_hash and project:
                tags = request.forms.get('tags')
                db.add_tags(file_hash, tags)
        if tag_action == 'del':
            if file_hash and tag_name:
                db.delete_tag(tag_name, file_hash)
        redirect('/file/{0}/{1}'.format(project, file_hash))

# Notes Add, Update, Delete
@app.route('/file/notes', method='POST')
def file_notes():
    db = Database()
    update = request.forms.get('update')
    new = request.forms.get('new')
    delete = request.forms.get('delete')
    note_title = request.forms.get('noteTitle')
    note_body = request.forms.get('noteBody')
    note_id = request.forms.get('id')
    note_sha = request.forms.get('sha256')
    project = request.forms.get('project')
    
    # Delete Note
    if delete and note_id:
        db.delete_note(note_id)
    # Update an existing note
    if update and note_id:
        db.edit_note(note_id, note_body)
    if new and note_sha and note_title and note_body:
        db.add_note(note_sha, note_title, note_body)
    redirect('/file/{0}/{1}#notes'.format(project, note_sha))
        

# Return Output from Module.
@app.route('/file/module', method='POST')
def run_module():      
    # Get the hash of the file we want to run a command against
    file_hash = request.forms.get('file_hash')
    if len(file_hash) != 64:
        file_hash = False
    # Lot of logic here to decide what command you entered.
    module_name = request.forms.get('module')
    command = request.forms.get('command')
    cmd_line = request.forms.get('cmdline')
    # cmd_line will override any other requests.
    if cmd_line:
        module_results = module_cmdline(cmd_line, file_hash)
    elif command:
        cmd_string = '{0} {1}'.format(module_name, mod_dicts[module_name][command])
        try:
            module_results = module_text(cmd_string, file_hash)
        except Exception as e:
            module_results = "The Command '{0}' generated an error. \n{1}".format(cmd_string, e)
    else:
        module_results = "You Didn't Enter A Command!"
    
    return '<pre>{0}</pre>'.format(str(parse_text(module_results)))
    

# Yara Rules
@app.route('/yara', method='GET')   
@app.route('/yara', method='POST')
def yara_rules():
    
    # Get list of Rules
    rule_path = os.path.join(VIPER_ROOT, 'data/yara')
    rule_list = os.listdir(rule_path)
    
    # GET is for listing Rules
    if request.method == 'GET':
        action = request.query.action
        rule = request.query.rule
        if action == 'list':
            # Return a list of rules.
            return template('yara.tpl', rule_list=rule_list, rule_text=False)
        if action == 'display' and rule:
            # Display Rule Contents
            rule_file = os.path.join(rule_path, rule)
            if os.path.isfile(rule_file):
                # Only allow .yar or .yara files to be read
                file_name, file_ext = os.path.splitext(rule_file)
                if file_ext in ['.yar', '.yara']:
                    rule_text = open(rule_file, 'r').read()
                    return template('yara.tpl', rule_text=rule_text, rule_name=rule, rule_list=rule_list)
                else:
                    return template('error.tpl', error="Unable to read file {0}".format(rule_file))
        # delete Rules
        if action == 'delete':
            rule_name = request.query.rulename
            if rule_name.split('.')[-1] in ['yar', 'yara']:
                os.remove(os.path.join(rule_path, rule_name))
            redirect('/yara?action=list')
            
    # POST is for adding / updating Rules
    if request.method == 'POST':
        rule_name = request.forms.get('rule_name')
        rule_text = request.forms.get('rule_text')
        rule_file = os.path.join(rule_path, rule_name)
        # Prevent storing files in a relative path or with a non yar extension
        rule_test = rule_name.split('.')
        if len(rule_test) == 2 and rule_test[-1] in ['yar', 'yara']:
            # if file exists overwrite
            with open(rule_file, 'w') as rule_edit:
                rule_edit.write(rule_text)
            redirect('/yara?action=display&rule={0}'.format(rule_name))
        else:
            return template('error.tpl', error="The File Name did not match the style 'name.yar'")

# Cuckoo Functions
@app.route('/cuckoo/submit', method='GET')
def cuckoo_submit():
    # Get Query Strings
    project = request.query.project
    file_hash = request.query.hash
    if project in project_list():
        __project__.open(project)
    else:
        __project__.open('../')
        project = 'Main'
    # Open the Database
    db = Database()
    # Open a session
    try:
        path = get_sample_path(file_hash)
        __sessions__.new(path)
    except:
        return '<span class="alert alert-danger">Invalid Submission</span>'   

    try:
        # Return URI For Existing Entry
        check_uri = '{0}/files/view/sha256/{1}'.format(cuckoo_api, file_hash)
        check_file = requests.get(check_uri)
        if check_file.status_code == 200:
            check_result =  dict(check_file.json())
            cuckoo_id = check_result['sample']['id']
            return '<a href="{0}/submit/status/{1}" target="_blank"> Link To Cukoo Report</a>'.format(cuckoo_web, str(cuckoo_id))
    except Exception as e:
        return '<span class="alert alert-danger">Error Connecting To Cuckoo</span>'
    
    # If it doesn't exist, submit it.
    
    # Get the file data from viper
    file_data = open(__sessions__.current.file.path, 'rb').read()
    file_name = __sessions__.current.file.name
   
    if file_data:
        # Submit file data to cuckoo
        uri = '{0}{1}'.format(cuckoo_api, '/tasks/create/file')
        options = {'file': (file_name, file_data)}
        cuckoo_response = requests.post(uri, files=options)
        if cuckoo_response.status_code == 200:
            cuckoo_id = dict(cuckoo_response.json())['task_id']
            return '<a href="{0}/submit/status/{1}" target="_blank"> Link To Cukoo Report</a>'.format(cuckoo_web, str(cuckoo_id))
    else:
        return '<span class="alert alert-danger">Unable to Submit File</span>'
    
# Hex Viewer
@app.route('/hex', method='POST')
def hex_viewer():
    # get post data
    file_hash = request.forms.get('file_hash')
    try:
        hex_offset = int(request.forms.get('hex_start'))
    except:
        return '<p class="text-danger">Error Generating Request</p>'
    hex_length = 256
    
    # get file path
    hex_path = get_sample_path(file_hash)
    
    # create the command string
    hex_cmd = 'hd -s {0} -n {1} {2}'.format(hex_offset, hex_length, hex_path)
    
    # get the output
    hex_string = getoutput(hex_cmd)

    # Format the data
    html_string = ''
    hex_rows = hex_string.split('\n')
    for row in hex_rows:
        if len(row) > 9:
            off_str = row[0:8]
            hex_str = row[9:58]
            asc_str = row[58:78]
            asc_str = asc_str.replace('"', '&quot;')
            asc_str = asc_str.replace('<', '&lt;')
            asc_str = asc_str.replace('>', '&gt;')
            html_string += '<div class="row"><span class="text-primary mono">{0}</span> <span class="text-muted mono">{1}</span> <span class="text-success mono">{2}</span></div>'.format(off_str, hex_str, asc_str)
    # return the data
    return html_string

# Cli Commands
@app.route('/cli')
def cli_viewer():
    return template('cli.tpl')

# VirusTotal Download
@app.route('/virustotal', method='POST')
def vt_download():
    vt_hash = request.forms.get('vt_hash')
    project = request.forms.get('project')
    tags = request.forms.get('tag_list')
    cmd_line = 'virustotal -d {0}; store; tags -a {1}'.format(vt_hash, tags)
    module_results = module_cmdline(cmd_line, False)
    if 'Stored' in module_results:
        redirect('/file/{0}/{1}'.format(project, vt_hash))
    else:
        return template('error.tpl', error="Unable to download file {0}".format(module_results))
    
    
# Run The web Server        
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-H', '--host', help='Host to bind the API server on', default='localhost', action='store', required=False)
    parser.add_argument('-p', '--port', help='Port to bind the API server on', default=9090, action='store', required=False)
    args = parser.parse_args()
    
    if args.port:
        web_port = args.port
    
    bv = bottle.__version__.split('.')[1]
    if int(bv) < 12:
        print "Please Upgrade Bottle to the latest version 'sudo pip install --upgrade bottle'"
        sys.exit()
    
    if not os.path.exists('projects'):
        os.mkdir('projects')
    
    # Set template dir
    bottle.TEMPLATE_PATH.insert(0,os.path.join(VIPER_ROOT, 'data/web'))
    app.run(host=args.host, port=web_port, reloader=True)

#!/usr/bin/env python
# This file is part of Viper - https://github.com/viper-framework/viper
# See the file 'LICENSE' for copying permission.

import os
import json
import copy
import argparse
import tempfile
import time

from bottle import route, request, response, run
from bottle import HTTPError

from viper.common.objects import File
from viper.core.storage import store_sample, get_sample_path
from viper.core.database import Database
from viper.core.session import __sessions__
from viper.core.plugins import __modules__
from viper.core.project import __project__
from viper.core.ui.commands import Commands

db = Database()

def jsonize(data):
    return json.dumps(data, sort_keys=False, indent=4)

@route('/test', method='GET')
def test():
    return jsonize({'message' : 'test'})

@route('/file/add', method='POST')
def add_file():
    tags = request.forms.get('tags')
    upload = request.files.get('file')

    tf = tempfile.NamedTemporaryFile()
    tf.write(upload.file.read())
    tf.flush()
    tf_obj = File(tf.name)
    tf_obj.name = upload.filename

    new_path = store_sample(tf_obj)

    success = False
    if new_path:
        # Add file to the database.
        success = db.add(obj=tf_obj, tags=tags)

    if success:
        return jsonize({'message' : 'added'})
    else:
        response.status = 500
        return jsonize({'message':'Unable to store file'})

@route('/file/get/<file_hash>', method='GET')
def get_file(file_hash):
    key = ''
    if len(file_hash) == 32:
        key = 'md5'
    elif len(file_hash) == 64:
        key = 'sha256'
    else:
        response.code = 400
        return jsonize({'message':'Invalid hash format (use md5 or sha256)'})

    db = Database()
    rows = db.find(key=key, value=file_hash)

    if not rows:
        response.code = 404
        return jsonize({'message':'File not found in the database'})

    path = get_sample_path(rows[0].sha256)
    if not path:
        response.code = 404
        return jsonize({'message':'File not found in the repository'})

    response.content_length = os.path.getsize(path)
    response.content_type = 'application/octet-stream; charset=UTF-8'
    data = ''
    for chunk in File(path).get_chunks():
        data += chunk

    return data

@route('/file/delete/<file_hash>', method='GET')
def delete_file(file_hash):
    success = False
    key = ''
    if len(file_hash) == 32:
        key = 'md5'
    elif len(file_hash) == 64:
        key = 'sha256'
    else:
        response.code = 400
        return jsonize({'message':'Invalid hash format (use md5 or sha256)'})

    db = Database()
    rows = db.find(key=key, value=file_hash)

    if not rows:
        response.code = 404
        return jsonize({'message':'File not found in the database'})
	
    if rows:
        malware_id = rows[0].id
        path = get_sample_path(rows[0].sha256)
        if db.delete_file(malware_id):
            success = True
        else:
            response.code = 404
            return jsonize({'message':'File not found in repository'})

    path = get_sample_path(rows[0].sha256)
    if not path:
        response.code = 404
        return jsonize({'message':'File not found in file system'})
    else:
        success=os.remove(path)
    
    if success:
        return jsonize({'message' : 'deleted'})
    else:
        response.code = 500
        return jsonize({'message':'Unable to delete file'})

@route('/file/find', method='POST')
def find_file():
    def details(row):
        tags = []
        for tag in row.tag:
            tags.append(tag.tag)

        entry = dict(
            id=row.id,
            name=row.name,
            type=row.type,
            size=row.size,
            md5=row.md5,
            sha1=row.sha1,
            sha256=row.sha256,
            sha512=row.sha512,
            crc32=row.crc32,
            ssdeep=row.ssdeep,
            created_at=row.created_at.__str__(),
            tags=tags
        )

        return entry

    for entry in ['md5', 'sha256', 'ssdeep', 'tag', 'name', 'all','latest']:
        value = request.forms.get(entry)
        if value:
            key = entry
            break

    if not value:
        response.code = 400
        return jsonize({'message':'Invalid search term'})

    # Get the scope of the search
    
    project_search = request.forms.get('project')
    projects = []
    results = {}
    if project_search and project_search == 'all':
        # Get list of project paths
        projects_path = os.path.join(os.getcwd(), 'projects')
        if os.path.exists(projects_path):
            for name in os.listdir(projects_path):
                projects.append(name)
        projects.append('../')
    # Specific Project
    elif project_search:
        projects.append(project_search)
    # Primary
    else:
        projects.append('../')

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
                project = 'default'
            proj_results.append(details(row))
        results[project] = proj_results
    return jsonize(results)

@route('/tags/list', method='GET')
def list_tags():
    rows = db.list_tags()

    results = []
    for row in rows:
        results.append(row.tag)

    return jsonize(results)
    
@route('/file/tags/add', method='POST')
def add_tags():
    tags = request.forms.get('tags')
    for entry in ['md5', 'sha256', 'ssdeep', 'tag', 'name', 'all','latest']:
        value = request.forms.get(entry)
        if value:
            key = entry
            break
    db = Database()
    rows = db.find(key=key, value=value)
    
    if not rows:
        response.code = 404
        return jsonize({'message':'File not found in the database'})
          
    for row in rows:
        malware_sha256=row.sha256
        db.add_tags(malware_sha256, tags)

    return jsonize({'message' : 'added'})

@route('/modules/run', method='POST')
def run_module():
    # Optional Project
    project = request.forms.get('project')
    # Optional sha256   
    sha256 = request.forms.get('sha256')
    # Not Optional Command Line Args
    cmd_line = request.forms.get('cmdline')
    if project:
        __project__.open(project)
    else:
        __project__.open('../')
    if sha256:
        file_path = get_sample_path(sha256)
        if file_path:
            __sessions__.new(file_path)
    if not cmd_line:
        response.code = 404
        return jsonize({'message':'Invalid command line'})
    results = module_cmdline(cmd_line, sha256)  
    __sessions__.close()
    return jsonize(results)

            
# this will allow complex command line parameters to be passed in via the web gui    
def module_cmdline(cmd_line, sha256):
    json_data = ''
    cmd = Commands()
    split_commands = cmd_line.split(';')
    for split_command in split_commands:
        split_command = split_command.strip()
        if not split_command:
            continue
        root = ''
        args = []
        # Split words by white space.
        words = split_command.split()
        # First word is the root command.
        root = words[0]
        # If there are more words, populate the arguments list.
        if len(words) > 1:
            args = words[1:]
        try:
            if root in cmd.commands:
                cmd.commands[root]['obj'](*args)
                if cmd.output:
                    json_data += str(cmd.output)
                del(cmd.output[:])
            elif root in __modules__:
                # if prev commands did not open a session open one on the current file
                if sha256:
                    path = get_sample_path(sha256)
                    __sessions__.new(path)
                module = __modules__[root]['obj']()
                module.set_commandline(args)
                module.run()

                json_data += str(module.output)
                del(module.output[:])
            else:
                json_data += "{'message':'{0} is not a valid command'.format(cmd_line)}"
        except:
            json_data += "{'message':'Unable to complete the command: {0}'.format(cmd_line)}"
    __sessions__.close()
    return json_data 
 
@route('/projects/list', method='GET')
def list_projects():
    projects_path = os.path.join(os.getcwd(), 'projects')
    if not os.path.exists(projects_path):
        response.code = 404
        return jsonize({'message':'No projects found'})
    rows = []
    for project in os.listdir(projects_path):
        project_path = os.path.join(projects_path, project)
        rows.append([project, time.ctime(os.path.getctime(project_path))])
    return jsonize(rows)


  
@route('/file/notes/<action>', method='POST')
def add_notes(action):
    note_title = request.forms.get('title')
    note_body = request.forms.get('body')
    note_sha = request.forms.get('sha256')
    note_id = request.forms.get('id')
    # Create New
    if action == 'add':
        if note_title and note_body:

            db.add_note(note_sha, note_title, note_body)
            return jsonize({'message' : 'Note added'})
        else:
            return jsonize({'message':'Missing note_title and / or note_body'})
    # Delete 
    elif action == 'delete':
        if note_id:
            db.delete_note(note_id)
            return jsonize({'message' : 'deleted'})
        else:
            return jsonize({'message' : 'missing note_id'})
    # Update
    elif action == 'update':
        if note_id and note_body:
            db.edit_note(note_id, note_body)
            return jsonize({'message' : 'Note updated'})
    # List
    elif action == 'view':
        note_list = {}
        malware = db.find(key='sha256', value=note_sha)
        if malware:
            notes = malware[0].note
            if notes:
                rows = []
                for note in notes:
                    note_list[note.id] = {'title':note.title, 'body':note.body}
            return jsonize({'message' : note_list})
        else:
            return jsonize({'message' : 'no sample found'})
    else:
        return jsonize({'message':'no action given'})    
        
    return jsonize({'message':'Unable to complete action'})


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-H', '--host', help='Host to bind the API server on', default='localhost', action='store', required=False)
    parser.add_argument('-p', '--port', help='Port to bind the API server on', default=8080, action='store', required=False)
    args = parser.parse_args()

    run(host=args.host, port=args.port)

import os
env = Environment(ENV = {'PATH' : os.environ['PATH']})

env = Environment(CPPPATH='src')
env['FRAMEWORKS'] = ['OpenGL', 'Foundation', 'Cocoa'] 

flags = '-Wall -pedantic -g'
libs = ['SDL','SDL_net','GL','GLU']

env.Append(CPPPATH = ['/opt/local/include/'])
print env['CPPPATH']

Program('server', ['server.cpp','game.cpp'], LIBS=libs, FRAMEWORKS=env['FRAMEWORKS'], LIBPATH='.', CPPPATH=env['CPPPATH'], CPPFLAGS=flags)
Program('client', ['client.cpp','game.cpp'], LIBS=libs, FRAMEWORKS=env['FRAMEWORKS'], LIBPATH='.', CPPPATH=env['CPPPATH'], CPPFLAGS=flags)


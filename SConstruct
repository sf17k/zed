import os
env = Environment(ENV = {'PATH' : os.environ['PATH']})

env = Environment(CPPPATH='src')
env['FRAMEWORKS'] = ['OpenGL', 'Foundation', 'Cocoa'] 

flags = '-Wall -pedantic -g'
libs = ['SDL','SDL_net','GL','GLU','SOIL']

env.Append(CPPPATH = ['/opt/local/include/'])
print env['CPPPATH']

Program('game', ['game.cpp'], LIBS=libs, FRAMEWORKS=env['FRAMEWORKS'], LIBPATH='.', CPPPATH=env['CPPPATH'], CPPFLAGS=flags)


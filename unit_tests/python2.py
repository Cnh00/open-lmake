# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import os
import os.path as osp

for d in os.environ['PATH'].split(':') :
	python2 = osp.join(d,'python2')
	if osp.exists(python2) : break
else :
	python2 = None

if __name__!='__main__' :

	import sys

	import lmake
	from lmake.rules import PyRule

	lmake.manifest = (
		'Lmakefile.py'
	,	'hello'
	,	'world'
	)

	class Cat(PyRule) :
		stems = {
			'File1' : r'.*'
		,	'File2' : r'.*'
		}
		target = '{File1}+{File2}'
		deps = {
			'FIRST'  : '{File1}'
		,	'SECOND' : '{File2}'
		}
		python = python2
		def cmd() :
			sys.stdout.write(open(FIRST ).read())
			sys.stdout.write(open(SECOND).read())

elif python2 :

	import ut

	print('hello',file=open('hello','w'))
	print('world',file=open('world','w'))

	ut.lmake( 'hello+world' , done=1 , new=2 )           # check targets are out of date
	ut.lmake( 'hello+world' , done=0 , new=0 )           # check targets are up to date
	ut.lmake( 'world+world' , done=1         )           # check reconvergence

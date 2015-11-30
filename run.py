#!/usr/bin/python

import sys
import ecto

from scripts.odm_app import ODMApp

from opendm import log
from opendm import config
from opendm import system

def usage():
	log.ODM_ERROR('USAGE: %s --project-path [project_path]' % sys.argv[0])
 	log.ODM_ERROR('OpenDroneMap app finished - %s' % system.now())
 	sys.exit(0)
		
if __name__ == '__main__':

	log.ODM_INFO('Initializing OpenDroneMap app - %s' % system.now())

	# Force to provide the images path
	if config.args.get('project_path') is None:
		usage()

	# create an instance of my App BlackBox
	# internally configure all tasks
	app = ODMApp(args=config.args)

	# create a plasm that only contains the BlackBox
	plasm = ecto.Plasm()
	plasm.insert(app)

	# execute the plasm
	plasm.execute(niter=1)

 	log.ODM_INFO('OpenDroneMap app finished - %s' % system.now())
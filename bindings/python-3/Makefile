
include ../../Makefile.env

all ::

$(MAKECMDGOALS) ::

install ::
	-/bin/rm -rf ./build ./dist ./__pycache__
	-(cd pvdd; python3 ./setup.py sdist --no-default --dist-dir ../dist)
	-(cd pvddsync; python3 ./setup.py sdist --no-default --dist-dir ../dist)
	-python3 -m pip install --upgrade ./dist/pvdd-*.tar.gz ./dist/pvddsync-*.tar.gz
	-/bin/rm -rf ./dist

uninstall cleaninstall ::
	pip uninstall pvdd pvddsync

clean ::
	/bin/rm -rf ./build ./dist ./__pycache__

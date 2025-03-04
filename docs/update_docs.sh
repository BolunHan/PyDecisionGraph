#!/bin/bash
sphinx-apidoc -o docs/source/ decision_tree/ -f
cd docs
make clean
make html
#!/bin/bash
sphinx-apidoc -o docs/source/ decision_graph/ -f
cd docs
make clean
make html
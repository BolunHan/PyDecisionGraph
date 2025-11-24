"""
Web UI module for visualizing decision trees with LogicGroups

This module provides:
- Flask-based web interface for decision tree visualization
- D3.js-powered interactive tree rendering
- Support for LogicNode and LogicGroup visualization
"""

from .main import show

__all__ = ['show']
# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#
import os
import sys
sys.path.insert(0, os.path.abspath('../..'))


# -- Project information -----------------------------------------------------

project = 'Debuda'
copyright = '2022, Tenstorrent'
author = 'Tenstorrent'
master_doc = 'index'

# -- General configuration ---------------------------------------------------

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = ['myst_parser', 'sphinxarg.ext', 'sphinx.ext.autodoc', 'sphinx.ext.napoleon', 'sphinx.ext.autosummary', 'sphinx.ext.autosectionlabel']

# Add any paths that contain templates here, relative to this directory.
templates_path = ['_templates']

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']


# -- Options for HTML output -------------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
#
#html_theme = 'alabaster'
html_theme = 'sphinx_rtd_theme'
html_logo = 'images/tt_logo.svg'
html_favicon = 'images/cropped-favicon-32x32.png'

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['_static']

autodoc_member_order = 'bysource'
add_module_names = False

source_suffix = {
    '.rst': 'restructuredtext',
    '.txt': 'markdown',
    '.md': 'markdown',
}

import debuda

def replicate (times=1, char="="):
    ret = ""
    for i in range(times):
        ret+=char
    return ret

def generate_commands_rst (f):
    commands = debuda.import_commands()
    command_list = { command_data['long'] : command_data for command_data in commands }

    render_commands_of_type (f, 'housekeeping', "Housekeeping commands", command_list)
    render_commands_of_type (f, 'low-level', "Low level access commands", command_list)
    render_commands_of_type (f, 'high-level', "High level access commands", command_list)
    render_commands_of_type (f, 'dev', "Debuda.py development-related commands", command_list)

def render_commands_of_type (f, ct, ct_name, command_list):
    # f.write (f"{replicate(len(ct), '#')}\n")
    f.write (f"{ct_name}\n")
    f.write (f"{replicate(len(ct_name), '^')}\n\n")

    for cname, c in command_list.items():
        cname = str(c['long']) + " ``" + str(c['short']) + "``"
        if c['type'] == ct:
            f.write (f"{cname}\n")
            under = replicate(len(cname)+5, '"')
            f.write (f"{under}\n\n")
            args = f"{c['arguments']}" if c['arguments'] else "No arguments"
            f.write (f"Arguments: **{args}**\n\n{c['description']}\n\n")
            if 'module' in c:
                f.write (f"{c['module'].__doc__}\n\n")

def setup(app):
    app.add_css_file('tt_theme.css')

    print ("Generating Debuda.py command help")
    with open("dbd/docs/public/debuda_py/commands.generated-rst", "w") as f:
        generate_commands_rst (f)
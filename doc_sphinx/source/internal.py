#!/usr/bin/env python

import re
from docutils import nodes
from sphinx.util.docutils import SphinxDirective
from sphinx.locale import _
from sphinx.directives.other import TocTree

def visit_internal_node(self, node):
    self.visit_admonition(node)


def depart_internal_node(self, node):
    self.depart_admonition(node)


def process_internal_nodes(app, doctree, fromdocname):
    if not app.tags.has('internal'):
        for node in doctree.traverse(internal):
            node.parent.remove(node)


class internal(nodes.Admonition, nodes.Element):
    pass


class InternalDirective(SphinxDirective):

    # this enables content in the directive
    has_content = True

    def run(self):
        internal_node = internal('\n'.join(self.content))
        internal_node.set_class('internal-users')
        internal_node += nodes.title(_('Internal users'), _('Internal users'))
        self.state.nested_parse(self.content, self.content_offset, internal_node)
        return [internal_node]


class TocTreeFilt(TocTree):

    def filter_entries(self, entries):
        exclude_patterns = self.state.document.settings.env.config.toc_filter_exclude
        entries_to_include = []
        for entry in entries:
            should_filter = False
            for pattern in exclude_patterns:
                if re.match(pattern, entry):
                    should_filter = True
                    break
            if not should_filter:
                entries_to_include.append(entry)
        return entries_to_include

    def run(self):
        # Remove all TOC entries that should not be on display
        self.content = self.filter_entries(self.content)
        return super(TocTreeFilt, self).run()


def setup(app):
    app.add_node(internal,
                 html=(visit_internal_node, depart_internal_node),
                 latex=(visit_internal_node, depart_internal_node),
                 text=(visit_internal_node, depart_internal_node))
    app.add_directive('internal', InternalDirective)
    app.connect('doctree-resolved', process_internal_nodes)
    app.add_config_value('toc_filter_exclude', [], 'env')
    app.add_directive('toctree-filt', TocTreeFilt)

    return {
        'version': '0.1',
        'parallel_read_safe': True,
        'parallel_write_safe': True,
    }

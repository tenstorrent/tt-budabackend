# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.

from collections import defaultdict
from itertools import dropwhile, takewhile
import logging
import sys

from .watcher import VCDWatcher


class VCDParser:
    """
  A parser object for VCD files.  Reads definitions and walks through the value changes.
  """

    def __init__(self, log_ident="VCDParser", log_level=logging.WARN):
        """ Optional log_ident allows for making the logger output unique """
        keyword_functions = {
            # declaration_keyword ::=
            "$comment": self.drop_declaration,
            "$date": self.save_declaration,
            "$enddefinitions": self.vcd_enddefinitions,
            "$scope": self.vcd_scope,
            "$timescale": self.save_declaration,
            "$upscope": self.vcd_upscope,
            "$var": self.vcd_var,
            "$version": self.save_declaration,
            # simulation_keyword ::=
            "$dumpall": self.vcd_dumpall,
            "$dumpoff": self.vcd_dumpoff,
            "$dumpon": self.vcd_dumpon,
            "$dumpvars": self.vcd_dumpvars,
            "$end": self.vcd_end,
        }

        self.logger = logging.getLogger(log_ident)
        self.logger.setLevel(log_level)

        self.keyword_dispatch = defaultdict(self.parse_error, keyword_functions)

        self.scope = []
        self.now = 0
        self.then = 0
        self.idcode2references = defaultdict(list)
        self.xmr_cache = {}
        self.end_of_definitions = False
        self.changes = {}
        self.watchers = []
        self.debug = False
        self.watched_changes = {}
        # tzhou cache lookups to improve performance
        self.xmr2id_cache = {}
        self.id2xmr_cache = {}

    # Convenience getters/setters
    def get_id(self, xmr):
        """Given a Cross Module Reference (XMR), find the associated VCD ID string"""
        search_path = xmr.split(".")
        self.logger.info("Searching `%s`", xmr)

        if xmr in self.xmr2id_cache:
            return self.xmr2id_cache[xmr]

        for id in self.idcode2references:
            for ref in range(len(self.idcode2references[id])):
                (var_type, size, reference) = self.idcode2references[id][ref]
                match = True
                for depth, node in enumerate(search_path):
                    var_type, name = reference[depth]
                    # print(f"{depth} -- {node} -- {name}")
                    if node == name:
                        continue
                    else:
                        match = False
                        break
                if match:
                    self.xmr2id_cache[xmr] = id
                    return id

        raise ValueError("No match for ", xmr)

    def get_xmr(self, id):
        """Given an ID, generate the hierarchical reference"""
        if id in self.xmr_cache:
            return self.xmr_cache[id]

        (type, size, refs) = self.idcode2references[id][0]
        xmr = ".".join([v for (k, v) in refs])
        self.xmr_cache[id] = xmr
        return xmr

    def get_nets(self):
        """Dump all the XMR/hierarchical paths in the VCD file"""
        ret = []
        for id in self.idcode2references:
            ret.append(self.get_xmr(id))
        return ret

    def register_watcher(self, watcher):
        """Add a watcher to the list, for evaluation at each time change"""
        self.watchers.append(watcher)

    def deregister_watcher(self, watcher):
        """Remove a watcher from the list"""
        self.watchers.remove(watcher)

    # Parsing helpers
    def scalar_value_change(self, value, id):
        """VCD file scalar value change detected, store for later"""
        self.changes[id] = value

    def vector_value_change(self, format, number, id):
        """VCD file vector value change detected, store for later"""
        self.changes[id] = (format, number)

    def parse_error(self, tokeniser, keyword):
        self.logger.warning(
            "Don't understand keyword `%s`, trying to continue...", keyword
        )

    # Declaration stuff
    def drop_declaration(self, tokeniser, keyword):
        next(dropwhile(lambda x: x != "$end", tokeniser))

    def save_declaration(self, tokeniser, keyword):
        self.__setattr__(
            keyword.lstrip("$"), " ".join(takewhile(lambda x: x != "$end", tokeniser))
        )

    def vcd_scope(self, tokeniser, keyword):
        self.scope.append(tuple(takewhile(lambda x: x != "$end", tokeniser)))

    def vcd_upscope(self, tokeniser, keyword):
        self.scope.pop()
        next(tokeniser)

    def vcd_var(self, tokeniser, keyword):
        data = tuple(takewhile(lambda x: x != "$end", tokeniser))
        # ignore range on identifier ( TODO  Fix this )
        (var_type, size, identifier_code, reference) = data[:4]
        reference = self.scope + [("var", reference)]
        self.idcode2references[identifier_code].append((var_type, size, reference))

    def vcd_enddefinitions(self, tokeniser, keyword):
        self.end_of_definitions = True
        self.drop_declaration(tokeniser, keyword)

        self.logger.info("Finished parsing definitions!")

        for watcher in self.watchers:
            watcher.update_ids()
            for id in watcher.get_watching_ids():
                self.watched_changes[id] = "x"
        self.logger.debug("idcode2references vars:")
        for code, mytype in self.idcode2references.items():
            self.logger.debug("{}: {}".format(code, mytype))

    # Simulation keywords are presently ignored...
    def vcd_dumpall(self, tokeniser, keyword):
        self.logger.info("Ignoring `$dumpall`...")
        pass

    def vcd_dumpoff(self, tokeniser, keyword):
        self.logger.info("Ignoring `$dumpoff`...")
        pass

    def vcd_dumpon(self, tokeniser, keyword):
        self.logger.info("Ignoring `$dumpon`...")
        pass

    def vcd_dumpvars(self, tokeniser, keyword):
        self.logger.info("Ignoring `$dumpvars`...")
        pass

    def vcd_end(self, tokeniser, keyword):
        if not self.end_of_definitions:
            parse_error(tokeniser, keyword)

    # Actual parse routines
    def update_time(self, next_time):
        """Reached an update point in time in the VCD - use the collected changes
     and update any watchers that are sensitive to a signal that has changed"""
        current_time = self.now
        if self.logger.getEffectiveLevel == logging.DEBUG:
            self.logger.debug(
                "End of time %s, processing sensitivity lists. Changes:", current_time
            )
            for change in self.changes:
                self.logger.debug(
                    "  %s: %s", self.get_xmr(change), self.changes[change]
                )

        # Check watcher sensitivity lists, maybe notify
        for watcher in self.watchers:
            update_needed = False
            activity = {}
            # TODO cache this?
            for id in watcher.get_sensitive_ids():
                if id in self.changes:
                    update_needed = True
                    activity[id] = self.changes[id]

            if update_needed:
                collected_changes = {}
                for id in watcher.get_watching_ids():
                    collected_changes[id] = self.watched_changes[id]

                watcher.notify(activity, collected_changes)

        self.update_watched_changes()
        self.changes = {}
        self.then = current_time
        self.now = next_time
        self.logger.debug("Time is now %s (was %s)", self.now, self.then)

    def update_watched_changes(self):
        """Watched changes is a persistent store of changes to the list of signals
       considered by all watchers. Here it is updated after any watcher
       updates from update_time, to store the 'new' values"""
        for id in self.watched_changes:
            if id in self.changes:
                self.watched_changes[id] = self.changes[id]

    def parse(self, fh):
        """Tokenize and parse the VCD file"""
        # open the VCD file and create a token generator
        tokeniser = (word for line in fh for word in line.split() if word)

        for count, token in enumerate(tokeniser):
            # parse VCD until the end of definitions
            if not self.end_of_definitions:
                self.keyword_dispatch[token](tokeniser, token)
            else:
                # Working through changes
                c, rest = token[0], token[1:]
                if c == "$":
                    # skip $dump* tokens and $end tokens in sim section
                    continue
                elif c == "#":
                    self.update_time(rest)
                elif c in "01xXzZ":
                    self.scalar_value_change(value=c, id=rest)
                elif c in "bBrR":
                    self.vector_value_change(
                        format=c.lower(), number=rest, id=next(tokeniser)
                    )
                else:
                    raise "Don't understand `{}` after {} words".format(token, count)

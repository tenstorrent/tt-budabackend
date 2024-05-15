#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import os, sys
import re, yaml
import logging

from vcd3 import *
from optparse import OptionParser

# Parser defaults
defaults = {}
defaults["vcd_file"] = '0-0-waveform.vcd'
defaults["cfg_file"] = os.path.dirname(os.path.realpath(__file__)) + '/yaml/config.yaml'
defaults["dbg_file"] = 'vcd_parse.log'
defaults["clock"] = 'i_clk'
defaults["reset"] = 'i_reset_n'
defaults["separator"] = ' '

# Commandline options
usage = "usage: %prog [options] <vcd-file>"
optparser = OptionParser(usage)
optparser.add_option("--config",
                    dest = "config",
                    default = defaults["cfg_file"],
                    help = "config yaml containing the signals to be parsed for output")
optparser.add_option("--debug",
                    dest = "debug",
                    action = "store_true",
                    default = False,
                    help = "debug dump to " + defaults["dbg_file"])
(options, args) = optparser.parse_args()

class TensixWatcher(VCDWatcher):

    def __init__(self, parser, hier, clk, rst_n, sensitive=[], watch=[], trackers=[]):
        self.in_reset = True
        self.hier     = hier
        self.clk      = clk
        self.rst_n    = rst_n
        super().__init__(parser, 
            [hier + '.' + s for s in sensitive], 
            [hier + '.' + s for s in watch], 
            trackers
        )

    def should_notify(self):
        # Called every time something in the sensitivity list changes
        # This watcher looks for reset to trigger, then notifies trackers only on
        # rising clock edges.
        clk   = self.hier + '.' + self.clk
        rst_n = self.hier + '.' + self.rst_n

        if self.get_id(rst_n) in self.activity:
            try: # exception if this is X or Z
                if self.get_active_2val(rst_n):
                    print("@{0!s} {1!s} out of RESET".format(self.parser.now, self.hier))
                    self.in_reset = False
                elif self.in_reset:
                    print("@{0!s} {1!s} in RESET".format(self.parser.now, self.hier))
                    self.in_reset = True
                    return False
            except ValueError:
                return False

        if (self.get_id(clk) in self.activity
            and self.get_active_2val(clk)
            and not self.in_reset):
            return True
        return False


class TensixTracker(VCDTracker):

    def configure(self, name, hier, control, payload, dump_limit):
        self.intf = name
        self.hier = hier
        self.control = []
        self.payload = []
        self.add_control(control)
        self.add_payload(payload)
        self.dump_limit = -1
        self.dump_count = 0
        self.separator = " "

    def set_dump_limit(self, limit):
        self.dump_limit = limit

    def set_sig_separator(self, sep):
        self.separator = sep

    def add_control(self, signals):
        for sig in signals:
            self.control.append(self.hier + '.' + sig)
    
    def add_payload(self, signals):
        for sig in signals:
            self.payload.append(self.hier + '.' + sig)

    def sig_name(self, sig):
        hier_len = len(self.hier) + 1
        if sig.startswith(self.hier):
            return sig[hier_len:]
        else:
            return sig

    def start(self):
        pass

    def update(self):
        if (self.dump_limit > 0 and self.dump_count >= self.dump_limit):
            return

        # TODO: add more protocol support
        # only ANDing protocol signals for now
        and_count = 0 

        for ctl in self.control:
            if isinstance(self[ctl], tuple):
                val = v2d(self[ctl])
            else:
                val = eval(self[ctl])

            if val == 1:
                and_count = and_count + 1
            else:
                return

            if (and_count == len(self.control)):
                dump = "@{0!s} {1!s}: ".format(self.parser.now, self.intf)
                for pd in self.payload:
                    #sig = self.hier_to(pd)
                    #val = v2d(getattr(self, pd))
                    val = v2d(self[pd])
                    dump = dump + "{0!s}=0x{1:x}{2!s}".format(self.sig_name(pd), val, self.separator)
                print(dump)
                self.dump_count = self.dump_count + 1
                return

def main():
    parser = VCDParser(log_level=logging.INFO)
    data = yaml.safe_load(open(options.config))	

    if options.debug:
        # handler = logging.FileHandler(defaults["dbg_file"], 'w')
        handler = logging.FileHandler('vcd_parse.log', 'w')
        parser.logger.addHandler(handler)

    watchers = []
    for node in data:
        control = []
        payload = []
        sensitive = []
        clock = defaults["clock"]
        reset = defaults["reset"]
        separator = defaults["separator"]
        dump_limit = -1

        # YAML populate signals to be parsed
        for sig in node['protocol']:
            control.append(sig)

        for sig in node['payload']:
            payload.append(sig)

        # YAML override of default clock and reset signals
        if 'clock' in node:
            clock = node['clock']

        if 'reset' in node:
            reset = node['reset']

        if 'dump_limit' in node:
            dump_limit = node['dump_limit']

        if 'separator' in node:
            separator = node['separator']

        # Sampling is senstive to clock edges and resets
        sensitive.append(clock)
        sensitive.append(reset)

        print('-'*80)
        print(node['name'])
        print('    Control signals: ', control)
        print('    Payload signals: ', payload)

        # Interface watcher config
        tracker = TensixTracker(parser)
        tracker.configure(node['name'], node['hier'], control, payload, dump_limit)

        # Interface optional settings
        tracker.set_dump_limit(dump_limit)
        tracker.set_sig_separator(separator)

        # TODO tzhou: optimize trackers at the same hier to use a common watcher
        watcher = TensixWatcher(parser, node['hier'],
            clk       = clock,
            rst_n     = reset,
            sensitive = sensitive,
            watch     = control+payload,
            trackers  = [tracker]
        )
        watchers.append(watcher)

    if (len(args) > 0) :
        vcdfile = args[0]
    else :
        vcdfile = defaults["vcd_file"]

    print('-'*80)
    with open(vcdfile) as vcd_file:
        parser.parse(vcd_file)

if __name__ == '__main__':
    main()

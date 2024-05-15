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


class VCDWatcher:
    """Signal watching class, intended to work with the `VCDParser` class.

    Provide a list of XMRs that the watcher is sensitive to (i.e., a clock to sample on) 
    and a list of signals to actually watch.

    The VCD parser will call `watcher.should_notify` when it sees a change to a signal 
    on the sensitivity list and provide the changes to all watched signals. You can
    subclass this class to gate when the watcher will start notifying trackers, e.g.
    only on rising clock edges.  The default implementation triggers with every change.

    When the watcher is notified, it checks if any registered trackers need to be
    activated, updates all the currently active trackers, and terminates any
    finished ones.
    """

    def __init__(self, parser, sensitive=[], watch=[], trackers=[]):
        """Parser object is required.
        Sensitivity and watch lists are recommended but not mandatory.
        Trackers are entirely optional.
        """
        self.parser = parser
        self.sensitive = []
        self._sensitive_ids = []
        self.watching = []
        self._watching_ids = []
        self.values = None
        self.activity = None

        self.trackers = [] + trackers
        for tracker in self.trackers:
            tracker.watcher = self
            tracker.parser = self.parser
            tracker.start()
        for signal in sensitive:
            self.add_sensitive(signal)
        for signal in watch:
            self.add_watching(signal)
        self.parser.register_watcher(self)

    # Getters/setters
    def __getitem__(self, name):
        id = self.get_id(name)
        if id:
            return self.values[id]
        else:
            raise KeyError

    def __hasitem__(self, name):
        id = self.get_id(name)
        return id

    def get_sensitive_ids(self):
        """Parser access function for sensitivity list ids"""
        return list(self._sensitive_ids.values())

    def get_watching_ids(self):
        """Parser access function for watch list ids"""
        return list(self._watching_ids.values())

    def get_id(self, signal):
        """Look up the signal id from a signal name and optional path"""
        if signal in self._watching_ids:
            return self._watching_ids[signal]
        else:
            return None

    def get2val(self, signal):
        """Attempt to convert a scalar to a numerical 0/1 value"""
        id = self.get_id(signal)
        if id in self.values:
            value = self.values[id]
            if value in "xXzZ":
                raise ValueError
            return eval(value)

    def get_active_2val(self, signal):
        """Attempt to convert a scalar to a numerical 0/1 value"""
        id = self.get_id(signal)
        if id in self.activity:
            value = self.activity[id]
            if value in "xXzZ":
                raise ValueError
            return eval(value)

    def add_sensitive(self, signal):
        """Add a signal to the sensitivity and watch lists"""
        self.sensitive.append(signal)
        self.watching.append(signal)

    def add_watching(self, signal):
        """Register a signal to be watched"""
        self.watching.append(signal)

    # actionable methods
    def notify(self, activity, values):
        """Manage internal data, update existing trackers, clean up finished ones"""
        self.activity = activity
        self.values = values

        if self.should_notify():
            for tracker in self.trackers:
                tracker.notify(self.activity, self.values)
                if tracker.finished:
                    self.trackers.remove(tracker)

    def update_ids(self):
        """Callback after VCD header is parsed, to extract signal ids"""
        self._sensitive_ids = {xmr: self.parser.get_id(xmr) for xmr in self.sensitive}
        self._watching_ids = {xmr: self.parser.get_id(xmr) for xmr in self.watching}

    # Subclass and override this method to gate tracker updating
    # (for instance, only on rising clock edges)
    def should_notify(self):
        # Called every time something in the sensitivity list changes
        return true

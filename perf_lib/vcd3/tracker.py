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


class VCDTracker:
    """Abstract tracker class. Subclass this to implement a tracker.
    Examples are provided."""

    def __init__(self, watcher=None):
        self.watcher = watcher
        self.parser = None
        self.finished = False
        self.trigger_count = 0
        self.activity = None
        self.values = None

    def __getitem__(self, name):
        id = self.watcher.get_id(name)
        if id:
            return self.values[id]
        else:
            raise KeyError

    def __hasitem__(self, name):
        id = self.watcher.get_id(name)
        return id

    def start(self):
        raise NotImplemented

    def notify(self, activity, values):
        self.trigger_count += 1
        self.activity = activity
        self.values = values
        self.update()

    def update(self):
        raise NotImplemented

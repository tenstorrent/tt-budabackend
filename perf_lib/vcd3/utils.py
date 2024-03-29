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


def v2d(value):
    # tzhou: need allow dealing with either tuple or single val
    if isinstance(value, tuple):
        fmt, data = value
        if fmt == "b":
            return eval("0b" + data)
        if fmt == "h":
            return eval("0x" + data)
        return eval(data)
    else:
        return eval(value)

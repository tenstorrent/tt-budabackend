# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import torch
from torch import nn
import torch.nn.functional as F


class MM_net(nn.Module):
    def __init__(self, config={"k": 784, "n": 10}):
        super(MM_net, self).__init__()
        self.fc = nn.Linear(config["k"], config["n"])

    def forward(self, x):
        x = self.fc(x)
        return x

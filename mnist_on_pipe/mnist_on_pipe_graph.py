# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
# Task:
# Migerate mnist_on_pipe to graph

# Structure
# Difference: There is no forward or backward queue, but with graph
# What nodes do we need?
# Each node can have input and output (recompute queue)
# what edges do we need?
# forward, backward
# Thus, middle has 2 edges.

# forward_node --> loss_node --> backward node
# How do we pass in data?
# How do we take advantage of recompute queue?
# What attributes do we need to maintain in CPU process wrapper? for future extension

# To do:
# 0. Introduce model and dataloader (pytorch)
# 1. Initialze CPUs processors for mnist_on_pipe (cpu_process_wrapper.py)
# 2. Create a graph with edges (process_graph.py)
import sys
import os
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import torch, argparse
from torchvision import datasets, transforms
from host_runtime.process_graph import *
from host_runtime.cpu_process_wrapper import CPUProcess
import torch.multiprocessing as mp
from torch import nn
from functools import partial
import torch.nn.functional as F
import torch.optim as optim

class MNIST_net(nn.Module):
    def __init__(self):
        super(MNIST_net, self).__init__()
        self.conv1 = nn.Conv2d(1, 32, 3, 1)
        self.conv2 = nn.Conv2d(32, 64, 3, 1)
        self.dropout1 = nn.Dropout(0.25)
        self.dropout2 = nn.Dropout(0.5)
        self.fc1 = nn.Linear(9216, 128)
        self.fc2 = nn.Linear(128, 10)

    def forward(self, x):
        x = self.conv1(x)
        x = F.relu(x)
        x = self.conv2(x)
        x = F.relu(x)
        x = F.max_pool2d(x, 2)
        x = self.dropout1(x)
        x = torch.flatten(x, 1)
        x = self.fc1(x)
        x = F.relu(x)
        x = self.dropout2(x)
        x = self.fc2(x)
        x = F.log_softmax(x, dim=1)
        return x

# Foward pass takes data itertor and model to outut a next move
def forward_pass(self):
    input = next(self.data_iterator)[0]
    out = self.model(input)
    return {"out": out}

# Backward pass takes the gradient pass in and model to update on the model
def backward_pass(self, out):
    input = next(self.data_iterator)[0]
    out = self.model(input)
    self.optimizer.zero_grad()
    out.backward(out) 
    self.optimizer.step() 

# Loss compute takes output from the forward pass and target, compute their loss and send it backward
def compute_loss(self, out):
    out = out['out']
    out.requires_grad = True
    target = next(self.data_iterator)[1]
    lout = self.loss(out, target)
    lout.backward()
    return {'out': out.grad}

def main():

    transform=transforms.Compose([
        transforms.ToTensor(),
        transforms.Normalize((0.1307,), (0.3081,))
        ])

    training_set = datasets.MNIST('../data', train=True, download=True,
                       transform=transform)
    training_loader = torch.utils.data.DataLoader(training_set)

    # Deal with node attributes

    # Using iterator?
    forward_iter = iter(training_loader)
    loss_iter = iter(training_loader)
    backward_iter = iter(training_loader)

    forward_queue = mp.Queue()
    backward_queue = mp.Queue()

    torch.set_num_threads(1)
    model = MNIST_net()
    model.share_memory()

    optimizer = optim.Adam(model.parameters(), lr=0.01)

    # Set attribute programatically?
    foward_node = CPUProcess(forward_pass, name = "forward", out_queue = forward_queue, no_input = True)
    setattr(foward_node, 'data_iterator', forward_iter)
    setattr(foward_node, 'model', model)
    
    loss_node = CPUProcess(compute_loss, name = "loss", in_queue = forward_queue, out_queue = backward_queue)
    loss = nn.NLLLoss()
    setattr(loss_node, 'loss', loss)
    setattr(loss_node, 'data_iterator', loss_iter)

    backward_node = CPUProcess(backward_pass, name = "backward", in_queue = backward_queue, no_output = True)
    setattr(backward_node, 'data_iterator', backward_iter)
    setattr(backward_node, 'model', model)
    setattr(backward_node, 'optimizer', optimizer)

    # Deal with processes
    g = ProcessGraph()
    g.add_processes([foward_node, loss_node, backward_node])
    g.run()

    g.join()

if __name__ == "__main__":
    main()
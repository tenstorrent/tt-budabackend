# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import torch
import torch.multiprocessing as mp
import torch.optim as optim
from torch.optim.lr_scheduler import StepLR

import queue


queue_list = []
module_list = []

def run_ttmodules():
    for element in module_list:
        print("Starting process", module_list)
        element.start()

def join_ttmodules():
    for element in module_list:
        element.join()
    for q in queue_list:
        q.close()

def connect_tt_modules(producer,consumer):
    # make queues for forward and backward linkage
    tmp_fw_queue = mp.Queue()
    tmp_bk_queue = mp.Queue()

    # connect them up
    producer.foq = tmp_fw_queue
    consumer.fiq = tmp_fw_queue
    consumer.boq = tmp_bk_queue
    producer.biq = tmp_bk_queue

    # register queues in queue registry
    queue_list.append(tmp_fw_queue)
    queue_list.append(tmp_bk_queue)

def connect_src_data_q(q, consumer):
    consumer.fiq = q
    queue_list.append(q)

def connect_loss_q(q, consumer):
    consumer.ltiq = q
    queue_list.append(q)

class TtModule(mp.Process):
    def __init__(self, nn_module, epochs, batches_in_epoch, inputs_in_batch, loss=None):
        super(TtModule, self).__init__()
        self.nn_module = nn_module
        self.optimizer = optim.Adam(nn_module.parameters(), lr=0.01)
        self.scheduler = StepLR(self.optimizer, step_size=1, gamma=0.7)
        self.fiq = None
        self.foq = None
        self.biq = None
        self.rcq = mp.Queue()
        self.ltiq = None
        self.loss = loss
        self.boq = None
        self.inputs_in_batch = inputs_in_batch
        self.batches_in_epoch = batches_in_epoch
        self.epochs = epochs
        module_list.append(self)

    def fwd(self):
        self.nn_module.eval()
        inp_cnt = 0
        assert self.inputs_in_batch != 0, "Input count in batch set to zero"
        assert self.fiq != None, "Forward input queue undefined"
        assert self.foq != None, "Forward output queue undefined"

        while(inp_cnt < self.inputs_in_batch):
            try:
                # Run forward pass and push result into foq
                fin = self.fiq.get(block=True,timeout=0.01)
                out = self.nn_module(fin)
                inp_cnt = inp_cnt + 1
                self.foq.put(out.detach())
                self.rcq.put(fin)

            except:
                continue

    def back(self):
        self.nn_module.train()
        inp_cnt = 0

        assert self.inputs_in_batch != 0, "Input count in batch set to zero"
        assert self.biq != None, "Backward input queue undefined"

        while(inp_cnt < self.inputs_in_batch):

            if not self.rcq.empty() and not self.biq.empty():
                bkin = self.biq.get(block=True,timeout=0.01) # if this excepts, we won't pop rcq
                rcin = self.rcq.get() # this is not an async queue, just local queue no exceptions possible
                rcin.requires_grad = True # Ensure pytorch knows we want autograd to this input
                out = self.nn_module(rcin)

                self.optimizer.zero_grad()
                out.backward(bkin)
                if self.boq != None: # the back path of the network stem might (will) not be hooked up
                    self.boq.put(rcin.grad) # send backward gradient onward
                self.optimizer.step()

                inp_cnt = inp_cnt + 1

    def loss_back(self):
        self.nn_module.train()
        inp_cnt = 0

        assert self.inputs_in_batch != 0, "Input count in batch set to zero"
        assert self.fiq != None, "Forward input queue undefined"
        assert self.ltiq != None, "Loss target queue undefined"

        while(inp_cnt < self.inputs_in_batch):
            if not self.fiq.empty() and not self.ltiq.empty():
                fwin = self.fiq.get(block=True,timeout=0.01)
                targ = self.ltiq.get(block=True,timeout=0.01)
                fwin.requires_grad = True # Ensure pytorch knows we want autograd to this input
                out = self.nn_module(fwin)
                lout = self.loss(out,targ)

                self.optimizer.zero_grad()
                lout.backward()
                print("Loss: ",lout.item())
                self.boq.put(fwin.grad) # send backward gradient onward
                self.optimizer.step()

                inp_cnt = inp_cnt + 1

    def run(self):
        for epoch in range(0,self.epochs):
            for batch in range(0,self.batches_in_epoch):
                if(self.loss == None):
                    self.fwd()
                    self.back()
                else:
                    self.loss_back()
            self.scheduler.step()

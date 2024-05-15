# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import print_function
import argparse
import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim
from torchvision import datasets, transforms
from torch.optim.lr_scheduler import StepLR

import torch.multiprocessing as mp
import tt_modules

class NetStem(nn.Module):
    def __init__(self):
        super(NetStem, self).__init__()
        self.conv1 = nn.Conv2d(1, 32, 3, 1)
        self.conv2 = nn.Conv2d(32, 64, 3, 1)
        self.dropout1 = nn.Dropout(0.25)
        self.dropout2 = nn.Dropout(0.5)
        self.fc1 = nn.Linear(9216, 128)


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
        return x

class NetMiddle(nn.Module):
    def __init__(self):
        super(NetMiddle, self).__init__()
        self.fc2 = nn.Linear(128, 10)

    def forward(self, x):
        x = self.fc2(x)
        return x

class NetEnd(nn.Module):
    def __init__(self):
        super(NetEnd, self).__init__()
        self.fc = nn.Linear(2,2)

    def forward(self, x):
        x = F.log_softmax(x, dim=1)
        return x

def feed(args, dataset, dataloader_kwargs, epochs, batches_in_epoch, inputs_in_batch, dq, ltq):
    train_loader = torch.utils.data.DataLoader(dataset, **dataloader_kwargs)

    cnt = 0
    for epoch in range(0, args.epochs):
        for batch_idx, (data, target) in enumerate(train_loader):
            if(cnt < inputs_in_batch):
                dq.put(data)
                ltq.put(target)
                cnt = cnt + 1
            else:
                break


def test(model, device, test_loader):
    model.eval()
    test_loss = 0
    correct = 0
    with torch.no_grad():
        for data, target in test_loader:
            data, target = data.to(device), target.to(device)
            output = model(data)
            test_loss += F.nll_loss(output, target, reduction='sum').item()  # sum up batch loss
            pred = output.argmax(dim=1, keepdim=True)  # get the index of the max log-probability
            correct += pred.eq(target.view_as(pred)).sum().item()

    test_loss /= len(test_loader.dataset)

    print('\nTest set: Average loss: {:.4f}, Accuracy: {}/{} ({:.0f}%)\n'.format(
        test_loss, correct, len(test_loader.dataset),
        100. * correct / len(test_loader.dataset)))


def main():
    # Training settings
    parser = argparse.ArgumentParser(description='PyTorch MNIST Example')
    parser.add_argument('--batch-size', type=int, default=64, metavar='N',
                        help='input batch size for training (default: 64)')
    parser.add_argument('--test-batch-size', type=int, default=1000, metavar='N',
                        help='input batch size for testing (default: 1000)')
    parser.add_argument('--epochs', type=int, default=14, metavar='N',
                        help='number of epochs to train (default: 14)')
    parser.add_argument('--lr', type=float, default=1.0, metavar='LR',
                        help='learning rate (default: 1.0)')
    parser.add_argument('--gamma', type=float, default=0.7, metavar='M',
                        help='Learning rate step gamma (default: 0.7)')
    parser.add_argument('--no-cuda', action='store_true', default=False,
                        help='disables CUDA training')
    parser.add_argument('--dry-run', action='store_true', default=False,
                        help='quickly check a single pass')
    parser.add_argument('--seed', type=int, default=1, metavar='S',
                        help='random seed (default: 1)')
    parser.add_argument('--log-interval', type=int, default=10, metavar='N',
                        help='how many batches to wait before logging training status')
    parser.add_argument('--save-model', action='store_true', default=False,
                        help='For Saving the current Model')
    args = parser.parse_args()
    use_cuda = not args.no_cuda and torch.cuda.is_available()

    torch.manual_seed(args.seed)

    device = torch.device("cuda" if use_cuda else "cpu")

    train_kwargs = {'batch_size': args.batch_size}
    test_kwargs = {'batch_size': args.test_batch_size}
    if use_cuda:
        cuda_kwargs = {'num_workers': 1,
                       'pin_memory': True,
                       'shuffle': True}
        train_kwargs.update(cuda_kwargs)
        test_kwargs.update(cuda_kwargs)

    transform=transforms.Compose([
        transforms.ToTensor(),
        transforms.Normalize((0.1307,), (0.3081,))
        ])
    dataset1 = datasets.MNIST('../data', train=True, download=True,
                       transform=transform)
    dataset2 = datasets.MNIST('../data', train=False,
                       transform=transform)
#    train_loader = torch.utils.data.DataLoader(dataset1,**train_kwargs)
    test_loader = torch.utils.data.DataLoader(dataset2, **test_kwargs)

    model_stem = NetStem()
    model_middle = NetMiddle()
    model_end = NetEnd()
    loss = nn.NLLLoss()

    stem_pipe = tt_modules.TtModule(model_stem, args.epochs, 938, 1)
    middle_pipe = tt_modules.TtModule(model_middle, args.epochs, 938, 1)
    end_pipe = tt_modules.TtModule(model_end, args.epochs, 938, 1, loss)

    # define source data and target data queues
    data_queue = mp.Queue()
    loss_target_queue = mp.Queue()

    # connect everything up
    tt_modules.connect_src_data_q(data_queue,stem_pipe)
    tt_modules.connect_tt_modules(stem_pipe, middle_pipe)
    tt_modules.connect_tt_modules(middle_pipe, end_pipe)
    tt_modules.connect_loss_q(loss_target_queue, end_pipe)

    tt_modules.run_ttmodules()

    feed(args, dataset1, train_kwargs, args.epochs, 1, 938, data_queue, loss_target_queue)

    tt_modules.join_ttmodules()


    # test(model, device, test_loader)

    # if args.save_model:
    #     torch.save(model.state_dict(), "mnist_cnn.pt")


if __name__ == '__main__':
    mp.set_start_method('spawn')
    main()

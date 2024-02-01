# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from tenstorrent.chip.remote_chip import RemoteWormholeChip
from tenstorrent import utility

Q_START = 0x11080
Q_SIZE = 192
NODE_Q_SIZE = 576
NODE_Q_SIZE2 = 320
WR_PTR_OFFSET = 0 + 8

q_name = [
        "HOST REQ Q",
        "ETH IN REQ Q",
        "HOST RESP Q",
        "ETH OUT REQ Q",
        "NODE REQ Q",
        "NODE RESP Q"
        ]

q_name_6_7 = [
        "HOST REQ Q",
        "ETH IN REQ Q",
        "HOST RESP Q",
        "ETH OUT REQ Q",
        "NODE OUT REQ Q",
        "NODE IN REQ Q",
        "NODE OUT RESP Q",
        "NODE IN RESP Q"
        ]

q_data = []
erisc_cores = {
        0:[9,0,5,0,0x27eea20],
        1:[1,0,0,0,0x27ee120],
        2:[8,0,5,0,0x27ee900],
        3:[2,0,0,0,0x27ee240],
        4:[7,0,5,0,0x27ee7e0],
        5:[3,0,0,0,0x27ee360],
        6:[6,0,5,0,0x27ee6c0],
        7:[4,0,0,0,0x27ee480],
        8:[9,6,5,6,0x27f5620],
        9:[1,6,0,6,0x27f4d20],
        10:[8,6,5,6,0x27f5500],
        11:[2,6,0,6,0x27f4e40],
        12:[7,6,5,6,0x27f53e0],
        13:[3,6,0,6,0x27f4f60],
        14:[6,6,5,6,0x27f52c0],
        15:[4,6,0,6,0x27f5080]
        }

def read_version(chip):
  version = chip.pci_read_xy(9,0,0,0x210)
  return version

def read_heartbeat(chip,x,y):
  
  read1 = chip.pci_read_xy(x, y, 0, 0x1c)
  read2 = chip.pci_read_xy(x, y, 0, 0x1c)
  alive = False
  if read1 != read2:
    alive = True
  return alive

def read_q(chip,x,y):
  q_data = []
  rd_addr = Q_START + WR_PTR_OFFSET*4
  for i in range (0, 4):
    q_data.append(chip.pci_read_xy(x, y, 0, rd_addr))
    q_data.append(chip.pci_read_xy(x, y, 0, rd_addr+16))
    rd_addr = rd_addr + Q_SIZE

  for i in range (0, 2):
    q_data.append(chip.pci_read_xy(x, y, 0, rd_addr))
    q_data.append(chip.pci_read_xy(x, y, 0, rd_addr+16))
    rd_addr = rd_addr + NODE_Q_SIZE

  return q_data

def read_q_6_7(chip,x,y):
  q_data = []
  rd_addr = Q_START + WR_PTR_OFFSET*4
  for i in range (0, 4):
    q_data.append(chip.pci_read_xy(x, y, 0, rd_addr))
    q_data.append(chip.pci_read_xy(x, y, 0, rd_addr+16))
    rd_addr = rd_addr + Q_SIZE

  for i in range (0, 4):
    q_data.append(chip.pci_read_xy(x, y, 0, rd_addr))
    q_data.append(chip.pci_read_xy(x, y, 0, rd_addr+16))
    rd_addr = rd_addr + NODE_Q_SIZE2

  return q_data

def print_q_status(chip):
  for index, noc_xy in erisc_cores.items():
    q_data = read_q(chip, noc_xy[0], noc_xy[1])
    q_status = ""
    status = f"{GREEN}Alive{ENDC}" if read_heartbeat(chip, noc_xy[0], noc_xy[1]) == True else f"{FAIL}Dead{ENDC} "
    for i in range (0, 6):
      wrptr = q_data[i*2]
      rdptr = q_data[i*2+1]
      qsize = 4 if i < 4 else 16
      occupancy = wrptr - rdptr if wrptr >= rdptr else wrptr + 2*qsize - rdptr
      color = GREEN
      if occupancy > 0:
          color = WARNING
      q_status += q_name[i] + f"[{color}{occupancy:02d}{ENDC}] "
    print(f"{HEADER}ETH{index:02d}:{status}:{ENDC} {q_status}")

def print_q_status_6_7(chip):
  for index, noc_xy in erisc_cores.items():
    q_data = read_q_6_7(chip, noc_xy[0], noc_xy[1])
    q_status = ""
    status = f"{GREEN}Alive{ENDC}" if read_heartbeat(chip, noc_xy[0], noc_xy[1]) == True else f"{FAIL}Dead{ENDC} "
    for i in range (0, 8):
      wrptr = q_data[i*2]
      rdptr = q_data[i*2+1]
      qsize = 4 if i < 4 else 8
      occupancy = wrptr - rdptr if wrptr >= rdptr else wrptr + 2*qsize - rdptr
      color = GREEN
      if occupancy > 0:
          color = WARNING
      q_status += q_name_6_7[i] + f"[{color}{occupancy:02d}{ENDC}] "
    print(f"{HEADER}ETH{index:02d}:{status}:{ENDC} {q_status}")


def main(chip, args):
  global HEADER
  global GREEN
  global WARNING
  global FAIL
  global ENDC

  args = utility.parse_args(args)
  colors = 1
  if 'colors' in args:
    colors = int(args.colors)

  HEADER = ''
  GREEN = ''
  WARNING = ''
  FAIL = ''
  ENDC = ''

  if (colors != 0):
    HEADER = '\033[95m'
    GREEN = '\033[32m'
    WARNING = '\033[93m'
    FAIL = '\033[31m'
    ENDC = '\033[0m'
 
  version = read_version(chip)
  if (version == 0x060650f3 or version >= 0x06067000):
    print_q_status_6_7(chip)
  else:
    print_q_status(chip)


// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tt_device.h"
int main () {
  // Create device
  auto device = new tt_SiliconDevice(tt_SocDescriptor());

  delete device;
  return 0;
}

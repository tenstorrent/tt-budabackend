from abc import abstractmethod
from functools import cached_property
from typing import Dict, Optional, Set
from tt_coordinate import OnChipCoordinate
import tt_util as util, tt_netlist
from tt_firmware import ELF, BUDA_FW_VARS

# All-encompassing structure representing a Debuda context
class Context:
    def __init__(self, cluster_desc_path, short_name):
        self.server_ifc = None # This will be set from outside
        self._cluster_desc_path = cluster_desc_path
        self.short_name = short_name

    @property
    @abstractmethod
    def netlist(self):
        pass

    @cached_property
    def devices(self):
        import tt_device

        device_ids = self.device_ids
        devices: Dict[int,tt_device.Device] = dict()
        for device_id in device_ids:
            try:
                device_desc_path = self.server_ifc.get_device_soc_description(device_id)
            except:
                device_desc_path = tt_device.get_soc_desc_path(device_id, self._run_dirpath)
            # util.INFO(f"Loading device {device_id} from {device_desc_path}")
            devices[device_id] = tt_device.Device.create(
                self.arch,
                device_id=device_id,
                cluster_desc=self.cluster_desc.root,
                device_desc_path=device_desc_path,
                context=self
            )
        return devices

    @cached_property
    def cluster_desc(self):
        if self._cluster_desc_path is None:
            raise util.TTException(f"We are running with limited functionality, cluster description is not available.")
        return util.YamlFile(self._cluster_desc_path)

    @cached_property
    def is_buda(self):
        return False

    @cached_property
    def device_ids(self) -> Set[int]:
        try:
            device_ids = self.server_ifc.get_device_ids()
            return util.set(d for d in device_ids)
        except:
            return self.netlist.get_device_ids()

    @cached_property
    def arch(self):
        try:
            return self.server_ifc.get_device_arch(min(self.device_ids))
        except:
            return self.netlist.get_arch()

    @property
    @abstractmethod
    def elf(self):
        raise util.TTException(f"We are running with limited functionality, elf files are not available.")

    @property
    @abstractmethod
    def epoch_id_address(self):
        raise util.TTException(f"We are running with limited functionality, elf files are not available.")

    @property
    @abstractmethod
    def eth_epoch_id_address(self):
        raise util.TTException(f"We are running with limited functionality, elf files are not available.")

    @abstractmethod
    def get_risc_elf_path(self, location: OnChipCoordinate, risc_id: int) -> Optional[str]:
        pass

    def elf_loaded(self, location: OnChipCoordinate, risc_id: int, elf_path: str):
        pass

    def __repr__(self):
        return f"context"

class BudaContext(Context):
    def __init__(self, netlist_filepath, run_dirpath, runtime_data_yaml, cluster_desc_path):
        super().__init__(cluster_desc_path, "buda")
        self._netlist_filepath = netlist_filepath
        self._run_dirpath = run_dirpath
        self._runtime_data_yaml = runtime_data_yaml

        # TODO: Make this lazy
        # Assign a device to each graph.
        for _, graph in self.netlist.graphs.items():
            graph.device = self.devices[graph.device_id()]
        self.netlist.devices = self.devices

    @cached_property
    def netlist(self):
        return tt_netlist.Netlist(self._netlist_filepath, self._run_dirpath, self._runtime_data_yaml)

    @cached_property
    def is_buda(self):
        return True

    @cached_property
    def elf(self):
        elf_files_to_load = {
            "brisc": f"{self._run_dirpath}/brisc/brisc.elf",
            "ncrisc": f"{self._run_dirpath}/ncrisc/ncrisc.elf",
        }
        if self.arch.lower() != "grayskull":
            elf_files_to_load["erisc_app"] = f"{self._run_dirpath}/erisc/erisc_app.elf"

        extra_vars = BUDA_FW_VARS if self.is_buda else None
        return ELF(elf_files_to_load, extra_vars=extra_vars)

    @cached_property
    def epoch_id_address(self):
        address, _ = self.elf.parse_addr_size("brisc.EPOCH_INFO_PTR.epoch_id")
        if address is None:
            raise util.TTException(f"Could not find address of epoch_id field in ELF file.")
        return address

    @cached_property
    def eth_epoch_id_address(self):
        if self.arch.lower() == "grayskull":
            raise util.TTException(f"There are no eth cores on grayskull.")
        address, _ = self.elf.parse_addr_size("erisc_app.EPOCH_INFO_PTR.epoch_id")
        # address = 0x20080 + context.elf.parse_address("@epoch_t.epoch_id")
        if address is None:
            raise util.TTException(f"Could not ind address of epoch_id field in erisc ELF file.")
        return address

    def get_risc_elf_path(self, location: OnChipCoordinate, risc_id: int):
        from tt_debug_risc import get_risc_name

        if self._run_dirpath:
            device = location._device
            epoch_id = device.get_epoch_id(location)
            graph_name = self.netlist.get_graph_name(epoch_id, device._id)
            if graph_name:
                graph = self.netlist.graph(graph_name)
                op_name = graph.location_to_op_name(location)
                if op_name:
                    risc_name = get_risc_name(risc_id)
                    if risc_name == "BRISC":
                        return f"{self._run_dirpath}/brisc/brisc.elf"
                    elif risc_name == "TRISC0":
                        return f"{graph.op_info[op_name]}/tensix_thread0/tensix_thread0.elf"
                    elif risc_name == "TRISC1":
                        return f"{graph.op_info[op_name]}/tensix_thread1/tensix_thread1.elf"
                    elif risc_name == "TRISC2":
                        return f"{graph.op_info[op_name]}/tensix_thread2/tensix_thread2.elf"
                    elif risc_name == "NCRISC":
                        return f"{self._run_dirpath}/ncrisc/ncrisc.elf"
                    elif risc_name == "ERISC":
                        return f"{self._run_dirpath}/erisc/erisc_app.elf"
                        # return f"{self._run_dirpath}/erisc/erisc_app.l1.elf"
                        # return f"{self._run_dirpath}/erisc/erisc_app.iram.elf"
        return None

    def __repr__(self):
        return f"BudaContext"

class LimitedContext(Context):
    def __init__(self, cluster_desc_path):
        super().__init__(cluster_desc_path, "limited")
        self.loaded_elfs = {} # (OnChipCoordinate, risc_id) => elf_path

    @cached_property
    def netlist(self):
        raise util.TTException(f"We are running with limited functionality, elf files are not available.")

    def get_risc_elf_path(self, location: OnChipCoordinate, risc_id: int) -> Optional[str]:
        return self.loaded_elfs.get((location, risc_id))

    def elf_loaded(self, location: OnChipCoordinate, risc_id: int, elf_path: str):
        self.loaded_elfs[(location, risc_id)] = elf_path

    def __repr__(self):
        return f"LimitedContext"

# TODO: We should implement support for Metal
class MetalContext(Context):
    def __init__(self, cluster_desc_path):
        super().__init__(cluster_desc_path, "metal")

    def __repr__(self):
        return f"MetalContext"

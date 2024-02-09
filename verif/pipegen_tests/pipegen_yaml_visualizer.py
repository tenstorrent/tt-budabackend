# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

from typing import List, Tuple, Dict, Optional
import argparse
import networkx as nx
from networkx.drawing.nx_agraph import pygraphviz_layout
import matplotlib.pyplot as plt

from pipegen_yaml_filter_utils import PipeGraph, PipeNode, GraphNode, BufferNode


class PipegenYamlVisualizer:
    """Class to wrap PipeGraph creation and plot its representation using networkx.
    
    Attributes
    ----------
    DEFAULT_BUFF_COLOR = "#1f77b4":
        Default color for regular buffer node.
    DEFAULT_PIPE_COLOR = "#ffbf00":
        Default color for regular pipe node.
    SCATTER_EDGE_LABEL = "scatter":
        Label to be shown on edges connecting scatter bufs to pipes.
    SCATTER_GROUP_PARENT_DIVISOR = 100000
        Buffers within scatter group have their ids differ in the last 5 digits 
        of `BufferNode.id` field. To get their parent buffer id, we divide them 
        with 100000.

    yaml_path:
        Path to pipegen.yaml file.
    png_path:
        Path to .png file where to store plot.
    figsize:
        Size of the plot.
    ungroup_scatter_bufs:
        If true, each scatter buf will be shown on plot. Otherwise, they will be
        grouped in one buf (their parent).
    nxgraph:
        networkx directed graph object in which buffer, pipes and edges
        between them will be added to form a complete presentation of PipeGraph.
    node_color_map:
        Dict to map each node in self.nxgraph to a color.
    edge_labels:
        Dict to map edges with labels on them.
    """

    DEFAULT_BUFF_COLOR = "#1f77b4"
    DEFAULT_PIPE_COLOR = "#ffbf00"
    SCATTER_EDGE_LABEL = "scatter"
    SCATTER_GROUP_PARENT_DIVISOR = 100000

    def __init__(self, yaml_path: str, png_path: str, 
                 figsize: Tuple[int, int], expand_scatter_bufs: bool) -> None:
        self.yaml_path: str = yaml_path
        self.png_path: str = png_path
        self.expand_scatter_bufs: bool = expand_scatter_bufs

        self.nxgraph: nx.Graph = nx.DiGraph()
        self.node_color_map: Dict[GraphNode, Tuple | str] = {}
        self.edge_labels: Dict[Tuple[GraphNode, GraphNode], str] = {}

        # Configure global plot figsize and dpi. 
        plt.rcParams["figure.figsize"] = figsize
        plt.rcParams["figure.dpi"] = 100
        self.ax = plt.subplot(111)
    
    def plot_pipe_graph(self) -> None:
        """Creates PipeGraph, creates nodes for each buffer and pipe and edges
        between them, plots and saves to `self.png_path`."""
        pipe_graph = self.__create_pipe_graph_from_yaml(self.yaml_path)

        for pipe in pipe_graph.pipes:
            # Add a node for the pipe itself.
            self.__add_pipe_node(pipe)
            # Add all input buffer nodes and edges from them to the pipe.
            self.__add_pipe_input_nodes(pipe)
            # Add all output buffer nodes and edges from pipe to them.
            self.__add_pipe_output_nodes(pipe)

        layout = pygraphviz_layout(self.nxgraph, prog="dot")
        nx.draw(self.nxgraph, with_labels=True, ax=self.ax, pos=layout, 
                node_size=8000, node_color=self.node_color_map.values())
        nx.draw_networkx_edge_labels(self.nxgraph, pos=layout, 
                                     edge_labels=self.edge_labels)
        plt.savefig(self.png_path)

    def __create_pipe_graph_from_yaml(self, yaml_path: str) -> PipeGraph:
        """Creates PipeGraph from `yaml_path`."""
        pipe_graph = PipeGraph()
        pipe_graph.parse_from_yaml(yaml_path)
        pipe_graph.expand_scatter_buffers()
        pipe_graph.find_pipes_connections()
        return pipe_graph

    def __add_pipe_input_nodes(self, pipe: PipeNode) -> None:
        """Add all input buffer nodes and edges from them to the `pipe`.
        
        If buffers are scatter buffers, color them differently across scatter
        groups.
        """
        for buffer in pipe.inputs:
            if buffer.is_scatter:
                group_base_id = self.__get_parent_base_id(buffer.scatter_group_parent.id)
                node_color = self.__get_group_color(group_base_id)
                edge_label = f"{PipegenYamlVisualizer.SCATTER_EDGE_LABEL}\n{group_base_id}"

                if self.expand_scatter_bufs:
                    # Show each scatter buf separately.
                    self.__add_buffer_node(buffer, node_color)
                    self.__add_edge(buffer, pipe, edge_label)
                else:
                    # Group them under one parent buf.
                    self.__add_buffer_node(buffer.scatter_group_parent, node_color)
                    self.__add_edge(buffer.scatter_group_parent, pipe, edge_label)
            else:
                self.__add_buffer_node(buffer)
                self.__add_edge(buffer, pipe)

    def __add_pipe_output_nodes(self, pipe: PipeNode) -> None:
        """Add all output buffer nodes and edges from pipe to them.
        
        If output buffers belong to pipe scatter group, color them differently 
        across groups.
        """
        for scatter_group_idx, buffer in enumerate(pipe.outputs):
            if isinstance(buffer, list):
                for b in buffer:
                    self.__add_buffer_node(b, self.__get_group_color(scatter_group_idx))
                    self.__add_edge(pipe, b)
            else:
                self.__add_buffer_node(buffer)
                self.__add_edge(pipe, buffer)

    def __add_pipe_node(self, pipe: PipeNode, color: Tuple | str = DEFAULT_PIPE_COLOR) -> None:
        """Add a node for `pipe` in self.nxgraph and color it."""
        self.__add_node(pipe, color)

    def __add_buffer_node(self, buffer: BufferNode, color: Tuple | str = DEFAULT_BUFF_COLOR) -> None:
        """Add a node for `buffer` in self.nxgraph and color it."""
        self.__add_node(buffer, color)

    def __add_node(self, node: GraphNode, color: Tuple | str) -> None:
        """Add node to self.nxgraph and color it."""
        self.nxgraph.add_node(node)
        self.node_color_map[node] = color

    def __add_edge(self, from_node: GraphNode, to_node: GraphNode, label: Optional[str] = None) -> None:
        """Add edge to self.nxgraph."""
        self.nxgraph.add_edge(from_node, to_node)
        if label:
            self.edge_labels[(from_node, to_node)] = label

    def __get_group_color(self, id: int) -> Tuple:
        """Get unique color for pipe/buffer scatter groups based on `id`."""
        colors = plt.get_cmap("tab20").colors
        return colors[id % len(colors)]

    def __get_parent_base_id(self, parent_id: int) -> int:
        """Get the base part of parent buffer id."""
        return parent_id // PipegenYamlVisualizer.SCATTER_GROUP_PARENT_DIVISOR

def main(yaml_path: str, png_path: str, figsize: Tuple[int, int], expand_scatter_bufs: bool):
    visualizer = PipegenYamlVisualizer(yaml_path, png_path, figsize, expand_scatter_bufs)
    visualizer.plot_pipe_graph()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--yaml-path', type=str, required=True, default=None, 
                        help='Path to yaml')
    parser.add_argument('--png-path', type=str, required=False, default="PipeGraph.png", 
                        help='Path to png to save graph in')
    parser.add_argument('--figsize', type=int, nargs='+', required=False, default=[30, 30], 
                        help='Pyplot figsize (adjust to better accommodate view)')
    parser.add_argument('--expand-scatter-bufs', action="store_true", default=False, 
                        help="Draw scatter buffers as separate bufs") 
    args = parser.parse_args()

    main(args.yaml_path, args.png_path, tuple(args.figsize), args.expand_scatter_bufs)